# Render Path (sub-project 2b-ii) Design

Part of the renderer integration (`PROMPT.md`). Done so far: the asset system, 2a Foundation (render thread, command queue, backends, shader path, ImGui) and 2b-i Scene foundation (EnTT scene, reflection, `.wscene`, linked prefab instances, async loading, editor panels). This spec is **2b-ii Render path**: drawing the scene. 2c (post-processing and settings panels) follows.

## Goal and success criteria

A scene loaded with `--scene` shows lit and shadowed in the editor's Viewport panel and fullscreen in the game project, on Vulkan, D3D11 and D3D12, with the render thread fed by commands from the main thread.

- `--render-test` and `--render-smoke` exit 0 on all three backends with GPU validation on; each check is shown to fail under a negative control.
- Debug, Release and Shipping build; the game project links no editor code.
- Uploads use the copy queue where the backend has one, and render the same image without it.

## Decisions carried in

- **Offscreen viewport:** the scene renders into its own target; the editor's Viewport panel shows it through an ImGui texture registry (built here). A game draws the same target fullscreen.
- **Forward+ (clustered forward), not deferred.** The user chose this against the recommendation to port the standalone's deferred G-buffer. Consequence: the geometry and lighting passes are written fresh around a depth prepass and a light-culling compute step, and 2c's AO, TAA and motion blur will need normal and velocity prepasses added instead of reading a G-buffer.
- **CPU frustum culling, one indexed draw per visible primitive.** The standalone's GPU-driven path (Hi-Z, indirect draws, LODs) is a later sub-project.
- **Copy queue enabled** for uploads (below).
- Conventions carried over: `wiwa` namespace, `WI_LOG`, no `std::cout`, tabs, premake only, shaders under `engine/shaders` cooked by the editor's shader importer to DXBC and SPIR-V (no compiler at run time), components store `AssetRef`, the render thread never calls `Engine::get_module`, no build-config macro decides editor vs game, docs under `docs/superpowers/` are not committed.

## Scene to render thread

**`SceneExtractor` (main thread, every frame):** walks `MeshRenderer`, `Light` and `Camera` and compares each with what it last sent, keyed by `EntityGuid`. It emits deltas into the frame's `RenderBatch`: `UpsertInstance`, `RemoveInstance`, `UpsertLight`, `RemoveLight`, `SetCamera`; one `ResetScene` when the active scene changes. An instance command carries the id, the world matrix, the mesh `AssetRef` and the material `AssetRef`s (the `MeshRenderer` override list, else the mesh's own). Never a `Handle<T>`.

**`RenderScene` (render thread, inside `RenderWorld`):** instances, lights and the camera in flat arrays by stable slot, with a guid-to-slot map. Bounds come from `MeshData` once the mesh is Ready. It does not touch the ECS; it only calls `AssetManager::load` (non-blocking).

## GPU resources

**`GpuResourceCache`** (render thread), keyed by resolved asset guid.

- The first instance that names an asset calls `load()` and keeps the handle; when Ready, the render thread uploads it and drops the CPU handle so the CPU copy can be evicted.
- **Meshes:** vertices and indices are suballocated from shared large buffers, one range per mesh.
- **Textures:** uploaded with their cooked mips; `TextureKind` picks the format (sRGB for color, linear for data, a normal-map format).
- **Materials:** a GPU record in one structured buffer indexed per draw, plus a small binding set per material (its textures and a sampler; D3D11 has no bindless descriptor tables). Until a texture is Ready the material uses fallbacks (white, flat normal, neutral ORM) and switches when it arrives.
- Uploads are spread over frames by a per-frame byte budget. An instance whose mesh is not Ready is skipped.
- **Hot reload:** the 2a reload listener marks the guid dirty; the cache drops that GPU object and rebuilds it.

## Copy queue

- **`UploadQueue` (render thread):** owns a command list on `CommandQueue::Copy` and a ring of staging memory. The cache records its uploads into it and submits early in the frame; the graphics queue then does `queueWaitForCommandList(Graphics, Copy, instance)` before the frame's graphics list, so what was uploaded in a frame is usable in that frame.
- **D3D12:** create a copy `ID3D12CommandQueue` and pass it as `pCopyCommandQueue`. **Vulkan:** request a *dedicated* transfer family through vk-bootstrap (`get_dedicated_queue(transfer)`) and pass it as `transferQueue`. **D3D11:** no copy queue; uploads use the immediate context.
- **Fallback (permanent):** with no usable copy queue (no dedicated family, creation or submission failure), `UploadQueue` records on the graphics queue; the cache cannot tell. A failed copy submission switches to the fallback and logs it.
- **Vulkan ownership:** NVRHI creates every buffer and image `EXCLUSIVE` and does no queue-family ownership transfers, so a resource written on the transfer queue and read on graphics would be invalid. Patch the vendored NVRHI (`engine/vendor/nvrhi`, our source): when a transfer queue exists, buffers and textures that are neither render targets nor UAVs are created `CONCURRENT` across the graphics and transfer families. Two spots: `vulkan-buffer.cpp` and `vulkan-texture.cpp`. Render targets stay exclusive.
- **Feasibility gate:** the plan's first task is a spike that uploads a mesh and a texture through the copy queue on Vulkan and D3D12 with GPU validation on and compares the result with the graphics-queue path. A backend that cannot be made validation-clean uses the fallback, and the plan records it.
- The Renderer panel shows which queue uploads use, bytes uploaded and upload time.

## The frame (render thread)

1. **Uploads:** the cache's budgeted uploads; changed instance, light and material records written to their buffers.
2. **Shadows:** one directional light gets cascaded shadow maps (up to 4, fitted to the camera frustum); up to a few spot lights get shadows in a shared atlas. Point-light shadows are out of scope. Casters are CPU-culled per cascade.
3. **Depth prepass:** opaque and alpha-masked geometry writes depth only.
4. **Light culling (compute):** lights are binned into a clustered grid (3D froxels in view space) built from the camera and the light list. Clustered rather than tiled, because depth-bounded tiles do not serve the transparent pass.
5. **Opaque forward pass:** visible primitives sorted by pipeline state and material, depth test equal to the prepass; PBR metallic-roughness (base color, ORM, normal map, emissive); the directional light and the cluster's local lights; cascaded and spot shadows; a flat hemisphere ambient (IBL is 2c). Output: RGBA16F HDR target.
6. **Background:** a flat or gradient color from `SceneSettings`.
7. **Transparent pass:** alpha-blended primitives sorted back to front, lit the same way, no depth write.
8. **Resolve:** a plain tonemap (ACES fit plus sRGB) from HDR to an 8-bit viewport target with a fixed exposure; a placeholder that 2c's post chain replaces.
9. **ImGui pass** (exists) draws to the backbuffer; the Viewport panel shows the viewport target.

- **Materials:** alpha modes (opaque, mask, blend) and double-sided are supported. Clearcoat, sheen and transmission are stored in the GPU record but not shaded.
- **Draws:** per-draw instance and material indices as push constants (NVRHI supports them on all three backends). No MSAA (TAA is 2c).
- **Shaders:** new HLSL under `engine/shaders` (depth, light cull, shadow, forward, tonemap, blit), reusing the standalone's PBR and cascade math, cooked by the existing importer.

## Viewport, camera, lights

- **`ViewportTarget`:** HDR + depth + 8-bit LDR, created at the size the Viewport panel requests (`SetViewportSize` command) and recreated on resize; rendering waits for the first size. A game has no panel: it renders at backbuffer size and a blit draws the LDR target fullscreen.
- **ImGui texture registry:** render-side map from id to `nvrhi::ITexture`; the panel draws a registered id with `ImGui::Image`; `ImGuiRenderPass` binds the right texture per draw command; the font atlas becomes entry 0.
- **Camera:** the primary `Camera` entity. The editor Viewport also has an editor fly camera (right-mouse look, WASD), separate from scene cameras; a game uses the scene's primary camera. With none, the viewport shows "no camera".
- **Lights:** directional, point and spot from `Light` (spot angles in degrees; a directional light points along its entity's forward axis). The hemisphere ambient and background color come from a `SceneSettings` block with defaults; the real panels are 2c.

## Errors and degradation

- A missing or failed mesh: the instance is skipped, logged once per asset. A failed texture uses its fallback. A failed material uses a visible magenta default.
- A shader failure keeps the previous pipeline in use (as in 2a); the Renderer panel shows the status.
- A resource that cannot be created (viewport targets, buffers, the light grid) gives a clear log and an error text in the viewport; the engine keeps running.
- The panel shows GPU memory used by the cache. There is no eviction in 2b-ii (deferred).
- Device loss follows 2a.

## Testing

`--render-smoke` (extended) and a new `--render-test`, on all three backends with GPU validation on, plus deliberate-break negative controls.

`--render-test` renders small scenes built in code at a fixed size into the viewport target, reads the LDR result back and checks pixels:

- a lit cube is brighter on the face toward the light than on the one away from it;
- a plane under a cube casting a directional shadow is darker in the shadow than outside it, and spot shadows behave the same;
- an alpha-masked quad shows the cutout; an alpha-blended quad lets the background through;
- a point light adds light only within its range (the clustered grid);
- emissive color appears with no lights.

Also checked:

- **Cross-backend:** the same scene on Vulkan, D3D11 and D3D12 matches within a tolerance (Y flip, depth range, sRGB).
- **Assets:** textures, meshes and materials reach the GPU and the fallback is replaced when the real texture arrives; a re-imported mesh or texture shows on the next frame; the Viewport resizes and minimizes without errors.
- **Copy queue:** on D3D12 and Vulkan uploads went through the copy queue (the stats say which), the result equals the graphics-queue path, and forcing the fallback renders the same image.
- **Scale:** a 20,000-instance scene keeps the render thread's CPU frame (extraction plus culling) under a budget recorded in Release, and the editor's main-thread frame stays inside the 2b-i commit budget.
- **Acceptance:** a model loaded by `--scene` shows lit and shadowed in the editor Viewport (screenshot checked) and in the game project fullscreen.

## Out of scope

IBL and the sky, bloom, TAA, tone-mapping options, AO, fog and decals (2c); point-light shadows; MSAA; GPU-driven culling and LODs; clearcoat, sheen and transmission shading; skinning, morphs and animation; terrain, vegetation, crowd and particles; GPU memory eviction.

## Task order (for the plan)

1. Spike: copy queue on Vulkan and D3D12 (device setup, NVRHI patch, upload through it, validation-clean or fall back).
2. `UploadQueue` with the copy queue and the graphics fallback; stats.
3. `RenderScene`, the extraction commands and `SceneExtractor`.
4. `GpuResourceCache`: meshes (shared buffers), textures, materials, fallbacks, hot reload.
5. `ViewportTarget`, the ImGui texture registry and the Viewport panel (shows a cleared target first).
6. Camera and the editor fly camera.
7. Depth prepass and the opaque forward pass, unlit then lit with the directional light.
8. Light culling (clustered grid) and local lights.
9. Cascaded and spot shadows.
10. Transparent pass, alpha modes, the resolve and the game's fullscreen blit.
11. `--render-test` (pixel checks, cross-backend) and `--render-smoke --scene`.
12. Acceptance on Debug, Release and Shipping, three backends.
