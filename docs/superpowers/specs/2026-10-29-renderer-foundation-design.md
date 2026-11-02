# Renderer Foundation Design (sub-project 2a)

Date: 2026-10-29
Status: Draft, awaiting review
Part of: NVRHI renderer integration. Builds on `2026-10-29-asset-system-design.md` (implemented).

## Where this fits

The standalone renderer (`C:\Users\victo\Downloads\nvrhi-renderer (12)\nvrhi-renderer`, about 79k lines, D3D11/D3D12/Vulkan through NVRHI) is integrated into the engine in three sub-projects, each with its own spec, plan and implementation:

- **2a Foundation (this spec):** premake port, the three backends, render thread and main-to-render hand-off, ImGui hand-off, cooked shaders, the render-side asset cache. Ends with a window showing ImGui on every backend.
- **2b Scene path:** EnTT components, scene extraction into render commands, GPU resources from asset handles (textures, meshes, materials), geometry, lighting, shadows. A model appears on screen.
- **2c Post and settings:** bloom, TAA, tonemap, AO, IBL, fog and so on, and the renderer settings split into ImGui panels.

Later, separately: terrain, vegetation, crowd, particles, decals, volumetric fog, clouds, user post effects.

## Decisions (from brainstorming)

- The standalone renderer replaces the engine's current `renderer/` code (`device.*`, `platform/vulkan`, old ImGui pass, `render_pass.h`, `shader_utils.h`). The engine `Window` module (GLFW) stays on the main thread.
- All three backends (Vulkan, D3D11, D3D12) are kept and always compiled; no build-configuration gating.
- Render thread with a command queue and a render-side scene mirror (not the ECS).
- ImGui UI is built on the main thread; its draw data is copied to the render thread.
- Shaders are cooked by the asset pipeline: each `.hlsl` is compiled from its one source into DXBC and SPIR-V and stored in the cooked Shader asset. No compiler is needed in a game.
- ECS components store `AssetRef`, never `Handle<T>`. Only the renderer holds handles, until upload is done.
- Engine shaders live in the `engine/` source root (`engine/shaders/`), cooked into `Library/engine`. Shaders users write go in `Assets/`.

## Threads and ownership

- **Main thread:** GLFW window, input, ECS, the ImGui context and all UI. A `RenderModule` (IModule) is the only entry point other engine code uses.
- **Render thread:** the backend (device, swapchain), the `Renderer` and its passes, the render-side mirror, all GPU resources, and creating GPU objects from loaded assets.
- **Hand-off:** during a frame the main thread appends commands to the current batch. `end_frame()` seals it with the frame time, the window size and the copied ImGui draw data, and pushes it to a bounded queue (default depth 2 frames ahead; `end_frame()` blocks when full). The render thread pops a batch, applies its commands to the mirror, then renders.
- **Back to main:** stats, pass timings, shader-compile status and errors go through a double-buffered snapshot the main thread reads for the UI. There are no commands back.
- **Resize and minimize:** main polls the window size each frame and sends a resize in the batch. A minimized window pauses rendering.
- **Shutdown:** main sends a quit command and joins the thread; the render thread calls `waitForIdle` before destroying resources.
- GPU frames in flight (CPU ahead of GPU) keep the standalone renderer's behavior and are independent of the queue depth.

## Assets on the render thread

- The `AssetManager` is thread-safe and CPU asset data is immutable once Ready, so the render thread requests and reads assets directly. Only GPU objects are created on the render thread.
- A `GpuResourceCache`, keyed by resolved asset GUID, holds the GPU object of each texture, mesh, material and shader. The first command that references an asset makes the render thread call `AssetManager::load` (non-blocking) and keep the handle. When the asset is Ready it uploads it and drops the CPU handle so the CPU copy can be evicted.
- An instance whose mesh is not Ready is skipped. A material uses neutral fallback textures (white albedo, flat normal, neutral ORM) and switches to the real ones as each becomes Ready.
- Uploads are spread over frames with a per-frame byte budget.
- The render side walks Mesh to Material to Texture itself, using the `AssetRef`s stored in the cooked assets.
- **Hot reload:** the renderer registers an `AssetManager` reload listener. The listener runs on the importer's thread, so it only pushes the GUID into a small locked list; the render thread drains it each frame, drops the GPU object, reloads the asset and rebuilds it. A re-cooked shader rebuilds the pipelines that use it, including those whose shader includes it.
- 2a delivers the cache, the upload queue and the shader path; textures, meshes and materials follow in 2b.

## Shaders

- **Cooking (editor project):** the Shader importer compiles each `.hlsl` into DXBC (D3DCompile, `vs_5_0`/`ps_5_0`/`cs_5_0`) and SPIR-V (`dxc -spirv`, `*_6_0`, `-DVULKAN=1`, as the standalone renderer does), for each entry point present (`VSMain`, `PSMain`, `CSMain`). D3D11 and D3D12 share DXBC. DXC is found through `VULKAN_SDK` or `PATH` and is needed only on the editor machine.
- **Asset format:** `ShaderData` holds a list of binaries (stage, target, entry point, bytes), replacing the source text. `.hlsli` files are imported as Shader assets with no binaries (they exist for dependency tracking).
- **Includes:** the importer parses `#include`s. Every included file is recorded as a dependency of the cooked asset, and the include handler resolves names in the `engine/shaders` and `Assets/` roots.
- **Re-cook on include change:** the `AssetDatabase` records the content hashes of extra source files an importer read (`ImportContext::add_source_dependency`). The up-to-date check includes them, and a change to a `.hlsli` re-imports every shader that listed it.
- **All or nothing:** if either target fails to compile, the import fails and shows the compiler's message. A machine without DXC cannot produce D3D-only shaders by accident.
- **Runtime:** the renderer picks the binary for its backend. A missing binary makes the shader Failed with a clear log; nothing is compiled at run time.
- Not ported: `ShaderCompiler`, `ShaderRequest`, `ShaderHotReloader` (about 1,100 lines). The user post-effect shaders (generated HLSL) are 2c.

## Backends and build

- The standalone `backend/` (`IRenderBackend`, Vulkan/D3D11/D3D12) becomes `engine/src/renderer/backend/`. NVRHI is built with its Vulkan, D3D11 and D3D12 parts. The engine library links Vulkan, vk-bootstrap, `d3d11`, `d3d12`, `dxgi` and `dxguid`. `d3dcompiler` is linked by the editor project only.
- **Selecting the backend:** `--api=vulkan|d3d11|d3d12`, default Vulkan. If the chosen backend cannot start, the reason is logged and the game exits; there is no silent fallback. `--gpu-validation` turns validation layers on.
- Premake only. No CMake files are brought over; dependencies already in the workspace (GLFW, NVRHI, vk-bootstrap, ImGui, GLM) are reused.
- **Conventions:** `wiwa` namespace, `WI_LOG` (no printf or `std::cout`), `Ref`/`Scope`, tabs, the engine's `JobSystem` (the renderer's own copy is dropped), `#include <pch.h>` first.

## ImGui hand-off

- Main thread: the ImGui context and the GLFW backend; `NewFrame` and `Render` run there and the `ImDrawData` is copied into the batch (command lists, vertices, indices, display size and scale).
- Fonts: the atlas is built on the main thread and sent once as an upload command when it changes.
- Render thread: the ImGui pass draws the copied data last with the cooked `imgui.hlsl`.
- ImGui image ids map to render-side textures through a registry; the registry arrives in 2b with the first renderer-owned texture to show.

## Settings pattern (used by 2c)

Plain-data `RenderSettings` structs live on the main thread, where the panels edit them. A changed struct is sent as a single command and the render thread swaps it in. 2a applies this only to the basics (vsync, API display).

## Deliverables (acceptance)

1. The solution builds with premake only in Debug, Release and Shipping. `--api` starts all three backends on this machine.
2. The window shows a cleared frame, the ImGui demo window and a "Renderer" stats panel (FPS, frame ms, API and adapter, shader status). Mouse and keyboard work on all three backends.
3. The render thread runs with the 2-frames-ahead queue. Main-thread frame time does not depend on GPU time. Resize, minimize and shutdown are clean, with no validation errors; the Vulkan present-layout error seen with the current engine code is gone.
4. `engine/shaders/imgui.hlsl` is cooked by the editor into DXBC and SPIR-V and loaded by the renderer through the `AssetManager`. Editing it, or a `.hlsli` it includes, rebuilds the pipeline while the editor runs.
5. `--render-smoke <frames>` renders N frames on the chosen backend and exits 0, or 1 if any validation message or error was logged. It is run on all three backends.

## Out of scope

Scene and ECS components, models, materials, lighting, shadows and post-processing (2b, 2c); terrain, vegetation, crowd, particles, decals, fog, clouds and user post effects; the standalone `main.cpp`, its scene file format, its tests and its CMake files; texture compression and mip streaming; a pipeline cache across runs.

## Risks and assumptions

- NVRHI's current premake in `engine/vendor/nvrhi` may only build the Vulkan part; adding D3D11 and D3D12 may need its DirectX-Headers thirdparty folder wired in. The plan starts with this.
- DXC must be present on the editor machine (Vulkan SDK). Cooking fails with an explanatory message otherwise.
- The queue depth of 2 frames ahead adds up to two frames of input-to-screen latency; it is a constant that can be lowered.
