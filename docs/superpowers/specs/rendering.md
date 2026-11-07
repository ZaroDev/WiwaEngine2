# Rendering

Code: `engine/src/renderer`, shaders in `editor/engine/shaders`. Verified by `--render-test` (checks in `renderer/test/*_checks.cpp`) and `--render-smoke <frames>`, on all three APIs with `--gpu-validation`.

## Threads and hand-off

- **Main thread:** GLFW window, input, ECS, the ImGui context. `RenderModule` (priority -50) is the only entry point other code uses.
- **Render thread:** backend (device, swapchain), `Renderer` and its passes, the render-side scene mirror, all GPU resources. It must **never** call `Engine::get_module` or touch the ECS or Lua.
- During a frame the main thread appends deltas to the current `RenderBatch`; `end_frame()` seals it (frame time, window size, copied ImGui draw data, debug-draw data, UI draw list) and pushes it to a bounded queue (depth 2 frames ahead; blocks when full). Queue depth adds up to two frames of latency; it is a constant.
- Back to main: stats, pass timings, shader status, pick results and the per-frame times go through double-buffered snapshots. There are no commands back.
- Resize: main sends the viewport size; a minimized window pauses rendering. Shutdown: quit command, join, `waitForIdle` before destroying resources.
- ImGui is built on the main thread and its draw data copied into the batch; the font atlas is sent once. ImGui texture ids map to render-side textures through a registry (entry 0 is the font atlas); the Viewport panel shows the viewport target through it. Offscreen views (views.md) add dynamic ids, `FIRST_VIEW_TEXTURE_ID + view id`.

## Backends

`renderer/backend` has Vulkan, D3D12 and D3D11 behind NVRHI, all always compiled. `--api=vulkan|d3d11|d3d12` (default Vulkan); a backend that cannot start logs and exits, with no silent fallback. D3D12 tries the other adapters when the fastest cannot present to the window. On the dev laptop D3D runs on the Intel iGPU and Vulkan on the RTX (accepted). Cross-backend images must match (mean < 3, max <= 40). Premake only, no CMake. `d3dcompiler` is linked by the editor only.

## Scene to render thread

`SceneExtractor` (main thread, every frame; the main view's, and one per offscreen view, see views.md) compares `MeshRenderer`, `Light`, `Camera` and scene settings with what it last sent, keyed by `EntityGuid`, and emits deltas: `UpsertInstance/RemoveInstance`, `UpsertLight/RemoveLight`, `SetCamera`, settings, one `ResetScene` when the active scene changes. Commands carry `AssetRef`s, never handles. `RenderScene` (render thread) keeps instances/lights/camera in flat arrays by stable slot with a guid-to-slot map; bounds come from `MeshData` once the mesh is Ready. `RenderInstance` keeps `PrevWorld` for velocity; `RenderScene::end_frame()` sets it after the passes are recorded.

## GPU resources

`GpuResourceCache` (render thread, keyed by asset guid): the first instance naming an asset calls `AssetManager::load` and keeps the handle; when Ready it uploads and drops the CPU handle so the CPU copy can be evicted. Uploads are spread over frames by a per-frame byte budget.

- **Meshes:** suballocated from shared large vertex/index buffers (`range_allocator.h`). An instance whose mesh is not Ready is skipped.
- **Textures:** uploaded with cooked mips; `TextureKind` picks the format (sRGB color, linear data, normal-map format).
- **Materials:** a 64-byte GPU record in a structured buffer indexed by material slot, plus a small binding set per material (textures + sampler; D3D11 has no bindless tables). Until a texture is Ready the material uses fallbacks (white, flat normal, neutral ORM). A failed material draws magenta. Custom-shader materials: materials.md.
- **Environments:** `EnvironmentCache` (guid-keyed) uploads cube, SH and the BRDF table through the `UploadQueue`.
- **Hot reload:** the reload listener marks the guid dirty; the render thread drops the GPU object and rebuilds; a re-cooked shader rebuilds every pipeline that uses it (including through includes). The old object stays in use until the new one is ready.
- No GPU memory eviction.

## Copy queue (`UploadQueue`)

Owns a command list on the copy queue and a staging ring; the graphics queue waits on it before the frame's graphics list, so what was uploaded in a frame is usable in that frame. D3D12: a copy `ID3D12CommandQueue`. Vulkan: a *dedicated* transfer family via vk-bootstrap. D3D11: none (immediate context). With no usable copy queue, or after a failed copy submission, it records on the graphics queue permanently (logged); callers cannot tell. `--no-copy-queue` and `RenderQuality::CopyQueue` force the fallback.

Vulkan ownership trap: NVRHI creates buffers/images `EXCLUSIVE` and does no queue-family transfers. The vendored NVRHI (`engine/vendor/nvrhi`, our source) is patched in `vulkan-buffer.cpp` and `vulkan-texture.cpp` so that, when a transfer queue exists, buffers and textures that are neither render targets nor UAVs are `CONCURRENT` across graphics and transfer families.

## The frame

Registration order in `render_module.cpp` (the whole list runs once per view: the offscreen views first, the main view last; views.md):

```
Shadows -> Depth+Normal+Velocity prepass -> Light culling -> AO
 -> Forward opaque -> Sky -> Forward blended -> Transparent velocity
 -> TAA -> Motion blur
 -> Exposure -> Bloom -> Tonemap+grading (HDR -> LDR) -> FXAA
 -> Game UI -> Selection outline -> Debug draw -> (Pick, on a click only) -> Blit -> ImGui
```

Forward+ (clustered forward), not deferred: a depth prepass and a compute light-culling step feed one forward pass. AO, TAA and motion blur therefore read normal and velocity targets written by the prepass, not a G-buffer.

- **Shadows:** one directional light gets up to 4 cascaded maps fitted to the camera frustum; a few spot lights share an atlas. No point-light shadows. Casters are CPU-culled per cascade.
- **Prepass:** opaque and masked geometry writes depth, an octahedral normal (RG16F) and velocity (RG16F). Viewport depth is `D32S8` (the stencil serves the selection outline; D3D11 makes it typeless to sample).
- **Light culling:** lights binned into a clustered froxel grid (clustered, not tiled, so the transparent pass is served).
- **Forward:** PBR metallic-roughness (base color, ORM, normal, emissive), directional + cluster lights, cascaded and spot shadows, ambient from diffuse SH + prefiltered specular x BRDF table x `AmbientIntensity` x AO (hemisphere ambient when the source is None). Alpha modes opaque/mask/blend and double-sided are supported; clearcoat, sheen and transmission are stored but not shaded. Per-draw instance and material indices are push constants. No MSAA. CPU frustum culling, one indexed draw per visible primitive; no GPU-driven culling or LOD.
- **Sky:** after opaque, before blended, depth tested equal to the far plane. File source draws the environment cube (mip 0) rotated/scaled by settings; Atmosphere runs an analytic shader every frame (so the sun moves smoothly) while a job thread rebuilds cube + SH when the sun or parameters change by more than half a degree (a request replaces one not yet started; the old environment stays until the new one is uploaded; a failed build keeps the previous). Solid background is the HDR clear colour. Reflections are the sky only (no probes).
- **Environment asset:** the `.hdr` importer cooks cube (RGBA16F with mips: mip 0 sky, other mips roughness-prefiltered) and 9 SH coefficients via `IblMath` (in the engine lib so importer and runtime share it). The BRDF table is built once at start on a job thread.
- **AO:** GTAO on depth + normals at full resolution, blurred, into one R8 texture sampled for ambient/indirect only. With AO off a 1x1 white texture is bound so the shader has no branch.
- **Exposure:** luminance histogram compute + resolve keep a one-float exposure buffer on the GPU; tonemap reads it. Auto off = manual EV.
- **Bloom:** soft-knee down/up chain added inside tonemap. **Tonemap:** operator (ACES, Reinhard, Neutral, None), grading (contrast, saturation, temperature, tint, vignette), sRGB encode. **FXAA** runs only when TAA is off.
- **Game UI:** `UiRenderPass` draws the UI draw list into the LDR image after FXAA (ui.md).
- Not implemented: fog, SSR, colour-LUT grading (`Lut` assets import but no pass uses them), user post effects, depth of field, skinning/animation.

## Temporal chain (velocity, TAA, motion blur)

- **Velocity** (RG16F, `ViewportTarget::velocity()`): screen UV delta between unjittered current and previous position. `GpuInstance` is `World, Normal, PrevWorld` (192 bytes, mirrored in `scene_common.hlsli`). A transparent-velocity pass writes blended items with alpha >= 0.5. Sky/empty pixels (depth == far) are reprojected from camera matrices alone. The previous camera resets (history dropped) on resize, scene reset, camera cut and toggling the editor camera override.
- **Skinning hook:** previous clip position comes from one function, `PreviousClipPosition(vertex, instance)` in `depth.hlsli`. A skinning feature must keep the previous bone palette per instance and apply it there; a check calls the hook so it cannot silently fall back to world-matrix motion.
- **Jitter:** while TAA is on the rasterising projection gets a Halton(2,3) offset (8 samples). Unjittered: culling, shadow cascades, picking, velocity, TAA history lookup. Jittered: everything rasterised and depth reconstruction that must match the depth buffer (AO, sky). Any shader reading a projection matrix must pick the right one. TAA off = zero jitter, `Projection == UnjitteredProjection`, and output bit-identical to before.
- **TAA** (compute, HDR): reads `hdr()`, depth, velocity, history; writes history then copies into `hdr()` so later passes are unchanged. YCoCg variance clipping, Catmull-Rom history, NaN/Inf sanitising, plus ghosting rejection: depth disocclusion against a kept previous-depth copy, velocity dilation over 3x3 closest depth, motion-adaptive feedback, and dropped history outputs the current frame only.
- **Motion blur** (compute, HDR): 16 taps along the velocity target, scaled by exposure time against the render thread's frame delta, clamped to `MaxBlurPixels`; identity under 1 px; runs after TAA.
- Targets (`ViewportTarget`): `velocity()`, `taa_history(i)` x2, `taa_depth_history()`, `post_scratch()`; all follow the viewport generation counter (wait-for-idle before recreation).

## Settings

- `SceneSettings` (scene.md): the look; reaches the render thread through the extractor.
- `RenderQuality` (`render_quality.json`, `render_quality.h`): shadow distance/cascades/split lambda/bias values, vsync, copy queue, bloom mips (2..6), AO slices (1..4) and steps (4..16), FXAA, TAA. `sanitized()` clears FXAA when TAA is set. Read at renderer start, written when the Settings panel (Renderer page) changes a value, sent to the render thread as a command applied at the start of the next frame. Missing file or keys give defaults.
- The anti-aliasing choice is Off / FXAA / TAA, exclusive (FXAA after a TAA resolve only blurs).

## Shaders

HLSL in `editor/engine/shaders` (user shaders in `Assets/`). The editor's shader importer cooks each file into **DXBC** (D3DCompile `*_5_0`, shared by D3D11 and D3D12) and **SPIR-V** (`dxc -spirv`, `*_6_0`, `-DVULKAN=1`) for each present entry point (`VSMain`, `PSMain`, `CSMain`). Nothing is compiled at run time; a game needs no compiler. DXC comes from `VULKAN_SDK` or `PATH` and is needed on the editor machine only. All-or-nothing: if either target fails the import fails and the previous cooked version stays in use. `#include`s are dependencies (editing a `.hlsli` re-cooks every user); `.hlsli` files import as binary-less Shader assets. At run time a missing binary for the backend makes the shader Failed with a clear log, and a failed shader keeps the previous pipeline.

**Binding ranges** (new passes must stay outside these; any pass using the frame layout must call `setPushConstants`, zeros if unused; UAVs must avoid u6/u7):

| Set / range | Use |
| --- | --- |
| frame: `b0/b1`, `t1..t9`, `s10/s11` | instances, materials, lights, clusters, cascades, spot atlas, environment, BRDF, samplers |
| material set 1: `t10..t13`, `s14` | base color, normal, ORM, emissive, material sampler |
| `t15` | ambient occlusion |
| skin set (the last set of every pass that draws meshes): `t22` palettes, `t23` skin vertices | GPU skinning, animation.md |
| surface set 3: `t16..t19`, `t20/t21`, `s15` | user textures, surface records, parameter bytes, sampler |

New passes use their own binding layouts rather than extending the frame set.

## Debug renderer, outline, picking

- **`DebugDraw`** (main thread only; nothing on the render thread calls it): `line`, `box`, `sphere`, `circle`, `cone`, `capsule`, `frustum`, `arrow`, `grid`, `axes`, filled `triangle/quad`; each with colour, optional duration (ticked when a batch is sealed) and `DepthMode` `Tested` / `AlwaysOnTop` / `Faded` (a quarter as strong where hidden). Lines are expanded to screen-space quads in the vertex shader (pixel width). At batch seal the vertices move into the `RenderBatch`; a per-frame vertex cap drops overflow with one log. Works in the game in every configuration, but `GameModule::init` switches it off unless the game is started with `--debug-draw`; the Game panel switches it off every frame it owns the render (editor.md). Draws in LDR after the outline, so post never touches gizmo colours. Lua and physics debug draw both go through it.
- **Selection outline:** stencil ref 1 write of the selected meshes, then a fullscreen pass outlines stencil edges at a configurable pixel width, dimmer where occluded by scene depth. Stencil is cleared per frame.
- **Picking (GPU ID buffer):** a click sends the viewport pixel; the next render frame draws an `R32_UINT` entity-id target (id + 1, 0 = nothing; light and camera icons as camera-facing quads), copies the pixel to a readback buffer and delivers it to main. Selection lands 1-2 frames after the click. The target exists only while a pick is pending. Custom `VertexOffset` geometry is not pickable or outlined (they use unmodified geometry).
- Viewport: `ViewportTarget` holds HDR + depth + LDR at the size the Viewport panel requests; a game has no panel, renders at backbuffer size and Blit draws LDR fullscreen. Camera is the primary `Camera`; the editor Viewport has its own fly camera.

## Stats

`RenderSnapshot` carries per-pass GPU times, scene/cache counters, upload stats and the per-frame render times that feed `PerfStats` (editor.md).

## Skinning

Skinned meshes are deformed in the vertex shaders by a joint palette (`scene_common.hlsli`: `SkinPoint`, `SkinNormal`; the previous palette for the velocity). Palettes arrive in `SceneDelta::PaletteUpdates`, the previous palette is rolled in `RenderScene::end_frame` like `PrevWorld`, skin vertices live in a per-page buffer next to the vertex buffer, and both are bound in a skin set that is the last set of every pass that draws meshes (`SceneRenderData::skin_layout`). The base vertex of a draw is a push constant because `SV_VertexID` excludes it on D3D. Everything is in animation.md.
