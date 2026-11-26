# CLAUDE.md

Guidance for Claude Code in this repository. Per-area documentation (what exists, why, traps) is in `docs/superpowers/documentation/` - read the matching doc before changing an area. Fix the doc in the same change when code and doc disagree.

## Build and run

Windows only (VS 2022, x64, `VULKAN_SDK` must be set). Premake generates the solution; `*.sln`/`*.vcxproj` are git-ignored. Re-run the generator after adding or deleting files.

```bat
vendor\premake\premake5.exe vs2022        :: from the repo root -> Wiwa2.sln  (vs2026 for VS2026)
msbuild Wiwa2.sln /p:Configuration=Release /p:Platform=x64
```

The `scripts\GenerateProjects*.bat` files do `cd ..` first: run them from `scripts\`, never from the root (from the root they generate nothing and msbuild builds the stale projects without a word). Build Release to verify; Debug only to debug a problem.

Configurations `Debug`, `Release`, `Shipping`; output in `bin/<cfg>-windows-x86_64/<project>`. `scripts/Build*2022.bat` are stale, don't use them. Projects: `wiwa` (static lib, `engine/`), `wiwa_editor` (`editor/`, debugdir `editor/`), `wiwa_game` (`game/`, debugdir `game/`), `wiwa_tests` (`tests/`, debugdir `editor/`), `wiwa_launcher` (`launcher/`, debugdir `launcher/`: the project list that starts the editor on a project, launcher.md). The game links only the engine, never editor code. The editor and the game contain no test code: every check lives in `wiwa_tests`, which compiles `editor/src` (minus `editor.cpp`) next to its own sources and links the engine.

### Verification

No test framework (the user decided a suite isn't needed). Smoke commands: run `wiwa_tests` with a flag; the exit code is the result (0 = pass). Run from `editor/` (`..\bin\Debug-windows-x86_64\wiwa_tests\wiwa_tests.exe --scene-smoke`, also copied to `editor\wiwa_tests.exe` by a build); with no flag it says it needs one and exits 1. **A smoke check is only trusted if it was seen to fail under a negative control.** After editing a shader, run `--render-smoke 100 --api=d3d11` before `--render-test`.

The test sources are in `tests/src/<area>/` (`*_smoke_test.cpp`, `anim_*_checks.cpp`, `demo_smoke.*`, `vfx_editor_smoke.*`, `render/*_checks.cpp`). The headless smokes run on a thread from `tests/src/test_app.cpp` (an `Editor` subclass, `editor/src/editor_app.h`). The panel, play, demo and VFX smokes are driven per frame by `EditorSmokeDriver` through `EditorTestSetup::OnFrame` (`editor_module.h`), which also gives the panel smokes scratch settings files so they never touch the project's. `--render-test` runs the checks of `tests/src/render` through `set_render_check_runner` (`render_module.h`) on the render thread. `--render-smoke` is the render module's own and `--scene-smoke-load` the game's: they check the real executables and stay in the engine and the game.

| Flag | Where | Checks |
| --- | --- | --- |
| `--asset-smoke` | tests | import, pack and load round trip, GUID stability, materials, shaders, build settings |
| `--scene-smoke` | tests | scene save, cook, load, prefab overrides, settings round trips |
| `--script-smoke` | tests | Lua runtime, play mode state machine, scene clone, LuaLS stubs |
| `--ui-smoke` / `--ui-editor-smoke` | tests | game UI (files, layout, interaction, edit model) / UI Editor panel for 130 frames |
| `--physics-smoke` / `--physics-panel-smoke` | tests | Jolt behaviour, queries, dispatch, repeatability, vehicles (tire model, assists, scene sync) / Physics panel for 130 frames, then the vehicle editor for 120 |
| `--input-smoke` / `--input-panel-smoke` | tests | input, actions, rebinding, `.winput`, Lua API / Input panel for 130 frames |
| `--audio-smoke` / `--audio-panel-smoke` | tests | audio module on the Null backend, soundbank asset and metadata, emitters and play-mode sync, settings, Lua API, event browser model, audition (and the Wwise backend in a `WWISESDK` build, with banks in `editor/assets/audio`) / Audio panel for 130 frames |
| `--ai-smoke` / `--ai-panel-smoke` | tests | behaviour tree format and validator, node semantics, blackboard, MoveTo on a baked navmesh, controllers, Lua leaves and the `AI` API, perception, the edit model, gizmo shapes / the tree editor in the Asset Editor for 120 frames. |
| `--nav-smoke` / `--nav-panel-smoke` | tests | `.wnav` format, components and settings, Recast bake, Detour queries, crowd agents, Lua API and arrival hooks, the bake model (worker, stale check, cancel), overlay shapes, path tool / Navigation panel for 130 frames. |
| `--anim-smoke` / `--anim-panel-smoke` | tests | skeleton import, pose math and nodes, Animator module, sockets, extraction, Lua API, animation graphs (file, state machine, blend spaces, layers, edit model) / Animation panel for 130 frames and the Graph editor on the demo warrior's graph |
| `--launcher-smoke` | tests | the launcher's core: recent list, project folders, Add Existing, creating a project from a template, finding and starting the editor; case-insensitive catalog lookup (launcher.md) |
| `--new-project-smoke` | tests | a project made by `create_project` gets its starter scene: runs a child `wiwa_tests --project <scratch> --starter-scene-child` (launcher.md) |
| `--project-dir-smoke` | tests | the `--project` argument: parsing, finding the engine's assets, the directory link a project gets to them (project-settings.md) |
| `--play-smoke` / `--demo-smoke` | tests | play mode / the demo end to end: the hub's menu loads a scene, then each feature scene plays, its checks hold, its HUD script runs and every button of its HUD's controls is clicked; the scripts and UI scene also drives its HUD, input and video and hot-reloads a script (it waits for the startup scan, so it never plays a stale cook) |
| `--vfx-editor-smoke` | tests | the VFX editor panel: edit model, preview, readback, effect thumbnail |
| `--video-smoke` / `--video-mf-smoke` | tests | the video system on the Null decoder (decoder, clock, player, sync, seek, loop, failures, audio ring, module on a UI rig, Lua `finished`/`video_*` run in `--script-smoke`) / Media Foundation on the demo clip, a generated clip with known tones, damaged files, the importer probe and a player on the worker (**logs "skipped" and passes without `editor/assets/video/Intro.mp4`**) |
| `--render-test` | tests | GPU pixel checks in `tests/src/render/*_checks.cpp`, run on the render thread |
| `--render-smoke <frames>` | any (engine) | render N frames then exit (1 if a validation message or error was logged) |
| `--scene-smoke-load` | game | load the `--scene` (or the pak's default scene) and quit |
| `--bench [seconds]` | game | measure the `--scene` with vsync off, optionally write every frame (`--bench-out`, `--bench-label`, `--bench-meta`) for `scripts/bench_report.py`; exit 0 when the average reaches `--bench-target` (benchmarking.md) |

Other flags: `--project <dir>` (any executable: work in that folder, so a project can live outside `editor/`; the editor links the engine's own assets into it, project-settings.md), `--write-demo-video` (tests: writes the demo clip `editor/assets/video/Intro.mp4` with Media Foundation), `--nav-bake <file>` (editor, with `--scene`: bakes the scene's first navmesh volume to the file), `--debug-draw` (game: a game starts with debug draw off), `--scene <path>`, `--pack <outdir>`, `--api=vulkan|d3d11|d3d12` (default Vulkan, no silent fallback), `--gpu-validation`, `--no-copy-queue`. Panel smokes run with ImGui asserts on.

## Architecture in one page

Modules (`IModule`, registered in `Engine`, [engine.cpp](engine/src/core/engine.cpp)), by update priority: `AssetManager` -100, `AssetPipelineModule` (editor) -90, `SceneModule` -80, `RenderModule` -50, `InputModule` -30, `PhysicsModule` -20, `UiModule` -10, `ScriptingModule` 0, `NavModule` 5, `AIModule` 6, `AnimationModule` 10, `DecalModule` 11, `ParticleModule` 12, `VideoModule` 13, `AudioModule` 20, plus `Window` and the editor's `EditorModule`. Reach a module with `Engine::get_module`, except from the render and physics threads, which must never call it.

Rules that cut across areas:

- **Editor vs game.** Separate projects over one engine lib. No build-config macro (e.g. `WI_SHIPPING`) may decide editor-vs-game behaviour. Anything that imports or cooks (importers, `AssetDatabase`, packager, shader compilation) lives in `editor/src/asset_pipeline`; the engine has only the runtime.
- **Threads.** The scene, ECS, Lua, input and UI are main thread. The render thread and the physics thread each own their world and talk to main through plain-data commands in and immutable snapshots out. Off-thread work uses immutable data.
- **Assets are CPU-side only** and never touch NVRHI. Components and settings store `AssetRef`, never `Handle<T>`; only the renderer holds handles. `AssetType` is written to files: append before `Last`.
- **Component fields are described once** (`WI_COMPONENT`/`WI_FIELD`, `ui_fields()`) and feed serializer, inspector, overrides, clone, Lua and stubs. Add a field there, not in each consumer.
- **Nothing is compiled at run time.** HLSL in `editor/engine/shaders` is cooked to DXBC and SPIR-V by the editor; `#include`s are tracked dependencies.
- **Scripts and physics run only while the game clock plays.** Editor Play runs on a clone of the edit scene; Stop discards it.

| Area | Doc | Entry points |
| --- | --- | --- |
| Assets, packing, settings files | [assets.md](docs/superpowers/documentation/assets.md) | `engine/src/assets`, `editor/src/asset_pipeline` |
| Scene, reflection, prefabs, loading | [scene.md](docs/superpowers/documentation/scene.md) | `engine/src/scene` |
| Rendering, shaders, post, TAA, debug draw | [rendering.md](docs/superpowers/documentation/rendering.md) | `engine/src/renderer`, `editor/engine/shaders` |
| Materials, surface shaders | [materials.md](docs/superpowers/documentation/materials.md) | `.wmat`, `.wshader` |
| Decals (projected `Decal` component, decal pass, `Decal.spawn`) | [decals.md](docs/superpowers/documentation/decals.md) | `engine/src/scene/decal_*`, `engine/src/renderer/passes/decal_pass.*`, `editor/engine/shaders/decal.hlsl` |
| VFX (GPU particles, `.wfx` effects, `ParticleSystem`, the VFX editor, `Vfx` Lua API) | [vfx.md](docs/superpowers/documentation/vfx.md) | `engine/src/scene/particle_*`, `engine/src/renderer/particles`, `engine/src/renderer/passes/particle_*`, `editor/src/asset_editor/vfx_*` |
| Lua scripting (user API: [docs/scripting.md](docs/scripting.md)) | [scripting.md](docs/superpowers/documentation/scripting.md) | `engine/src/scripting` |
| Physics (Jolt) | [physics.md](docs/superpowers/documentation/physics.md) | `engine/src/physics`, vendored `engine/vendor/JoltPhysics`; `JPH::` stays inside it |
| Vehicles (`Vehicle`, `VehicleWheel`, `.wvehicle`, tire model, assists, Lua `vehicle`) | [vehicles.md](docs/superpowers/documentation/vehicles.md) | `engine/src/physics/vehicle_*`, `engine/src/physics/tire_model.*`, `editor/src/asset_editor/vehicle_*` |
| Animation (skeletons, GPU skinning, `Animator`, animation graphs `.wgraph`, Lua `Animation`) | [animation.md](docs/superpowers/documentation/animation.md) | `engine/src/animation`, `editor/src/animation` |
| Offscreen views (previews, 3D UI widgets, readback; `ViewHost`, `ViewImage`) | [views.md](docs/superpowers/documentation/views.md) | `engine/src/renderer/view`, `engine/src/scene/preview_scene.*` |
| Asset Editor, asset thumbnails (skeleton, mesh, clip, graph, VFX and behaviour tree editors, sockets, joint rename) | [editor.md](docs/superpowers/documentation/editor.md) | `editor/src/asset_editor` |
| Audio (Wwise behind a vendor-neutral backend, soundbanks, emitters) | [audio.md](docs/superpowers/documentation/audio.md) | `engine/src/audio`, `engine/src/audio/wwise` (`AK::` stays inside it), `editor/src/audio` |
| Behaviour trees and perception (`.wbt`, `AIController`, Lua leaves `BtAction`/`BtCondition`, `Perceiver`, the tree editor) | [behavior-trees.md](docs/superpowers/documentation/behavior-trees.md) | `engine/src/ai`, `editor/src/ai` |
| Navigation (Recast bake in the editor, Detour queries and DetourCrowd agents, `.wnav`, the `Nav` Lua API) | [navigation.md](docs/superpowers/documentation/navigation.md) | `engine/src/nav`, `editor/src/asset_pipeline/nav_bake.*`, `editor/src/nav`; vendored `engine/vendor/recastnavigation`; `dt*` stays in `engine/src/nav`, `rc*` in the editor |
| Video (`Video` UI widget, mp4 playback with sound, `IVideoDecoder` platform seam, Media Foundation, `video_*` Lua API) | [video.md](docs/superpowers/documentation/video.md) | `engine/src/video`, `engine/src/video/mf` (`IMF*` stays inside it), `engine/src/renderer/video`, `engine/src/ui/ui_video_fit.*`, `editor/src/asset_pipeline/importers/video_importer.cpp` |
| Benchmarking (`--bench`, per-frame recording, HTML reports with hotspots and baselines) | [benchmarking.md](docs/superpowers/documentation/benchmarking.md) | `engine/src/core/bench_recorder.*`, `game/src/game.cpp`, `scripts/bench_stress.py`, `scripts/bench_report.py` |
| Input | [input.md](docs/superpowers/documentation/input.md) | `engine/src/input` |
| Game UI | [ui.md](docs/superpowers/documentation/ui.md) | `engine/src/ui`, `editor/src/ui` |
| Project settings, Settings page | [project-settings.md](docs/superpowers/documentation/project-settings.md) | `engine/src/core/project_settings.*`, `editor/src/settings` |
| Project launcher (`wiwa_launcher`, recent projects, new projects and their starter scene, Switch Project) | [launcher.md](docs/superpowers/documentation/launcher.md) | `launcher/src`, `editor/src/project` |
| Editor, play mode, gizmos, performance stats | [editor.md](docs/superpowers/documentation/editor.md) | `editor/src` |

Animation in one line (the warrior of the demo plays `Warrior_animation/warrior.wgraph`, written by `scripts/gen_demo_graph.py`, run before `gen_demo_scene.py`): `AnimationModule` (after the scripts) evaluates a tree of pose nodes per `Animator` into a joint palette; palettes ride the scene delta to the render thread, which keeps the previous palette like `PrevWorld`, and vertex shaders skin through their own last binding set (`t22`/`t23`). The demo is written by `scripts/gen_demo_scene.py`: a hub (`editor/assets/scenes/demo.wscene`, the default scene) whose menu loads one scene per group of features (`demo_<group>.wscene`, `DEMO_SCENES`), each with a HUD of its own (`ui/demo_<group>_hud.wuipanel`: what it shows, controls driven by `scripts/<group>_ui.lua`, and Previous / Menu / Next through `demo_nav.lua`), with CC0 FBX characters (the warrior's footstep events live in its `.wanim` clips, extracted from the model once, `<Model>_animation/`; the scene points at the GUIDs and legacy sub-asset ids in the models' `.meta` files, which redirect to the extracted assets; `editor/assets/` is git-ignored, so the scripts regenerate all of it).

Renderer in one line: the render thread owns all GPU state and a GUID-keyed `GpuResourceCache`, is fed per-frame deltas through a bounded queue, and runs Forward+ (shadows, depth/normal/velocity prepass, light culling, AO, decals, particle simulation, forward, sky, particles, TAA, motion blur, exposure, bloom, tonemap, FXAA, UI, outline, debug, blit, ImGui).

## Conventions

- `wiwa` namespace, tabs, `#include <pch.h>` first in engine files (`<editor_common.h>` in editor files), `Ref`/`Scope` smart pointers.
- Log with `WI_LOG` and its area variants (`WI_RENDER_LOG`, `WI_PHYSICS_LOG`, `WI_ASSET_LOG`...), never `std::cout`/`printf`.
- `near` and `far` are Windows macros: don't use them as identifiers.
- Compile definitions that change how shared headers compile must be identical in every project that includes them: `JoltDefines()` (Jolt) and `SolDefines()` (`SOL_ALL_SAFETIES_ON`, so Lua argument checks behave the same in Release and Debug) in the root `premake5.lua`. A new project that includes Jolt or sol headers calls them.
- Premake only (no CMake). Third-party code is vendored under `engine/vendor/`; don't edit it except the patched NVRHI (rendering.md).
- Every new feature must be showcased in the demo scene of its group (a new group gets a scene of its own: `DEMO_SCENES` in `scripts/gen_demo_scene.py`), with controls in that scene's HUD, and covered by `--demo-smoke` (`tests/src/play/demo_smoke.cpp`, `SEGMENTS`).
- `docs/superpowers/documentation/` is committed; `docs/superpowers/plans/` and `PROMPT.md` are git-ignored and never committed. Plans are scratch and may be stale.
