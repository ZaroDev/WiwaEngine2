# CLAUDE.md

Guidance for Claude Code in this repository. Per-area design (what exists, why, traps) is in `docs/superpowers/specs/` - read the matching doc before changing an area. Fix the doc in the same change when code and doc disagree.

## Build and run

Windows only (VS 2022, x64, `VULKAN_SDK` must be set). Premake generates the solution; `*.sln`/`*.vcxproj` are git-ignored. Re-run the generator after adding or deleting files.

```bat
scripts\GenerateProjects2022.bat        :: premake5 vs2022 -> Wiwa2.sln  (GenerateProjects2026.bat for VS2026)
msbuild Wiwa2.sln /p:Configuration=Debug /p:Platform=x64
```

Configurations `Debug`, `Release`, `Shipping`; output in `bin/<cfg>-windows-x86_64/<project>`. `scripts/Build*2022.bat` are stale, don't use them. Projects: `wiwa` (static lib, `engine/`), `wiwa_editor` (`editor/`, debugdir `editor/`), `wiwa_game` (`game/`, debugdir `game/`). The game links only the engine, never editor code.

### Verification

No test framework (the user decided a suite isn't needed). Smoke commands: run the executable with a flag; the exit code is the result (0 = pass). Run from `editor/` (`..\bin\Debug-windows-x86_64\wiwa_editor\wiwa_editor.exe --scene-smoke`). **A smoke check is only trusted if it was seen to fail under a negative control.** After editing a shader, run `--render-smoke 100 --api=d3d11` before `--render-test`.

| Flag | Where | Checks |
| --- | --- | --- |
| `--asset-smoke` | editor | import, pack and load round trip, GUID stability, materials, shaders, build settings |
| `--scene-smoke` | editor | scene save, cook, load, prefab overrides, settings round trips |
| `--script-smoke` | editor | Lua runtime, play mode state machine, scene clone, LuaLS stubs |
| `--ui-smoke` / `--ui-editor-smoke` | editor | game UI (files, layout, interaction, edit model) / UI Editor panel for 130 frames |
| `--physics-smoke` / `--physics-panel-smoke` | editor | Jolt behaviour, queries, dispatch, repeatability / Physics panel for 130 frames |
| `--input-smoke` / `--input-panel-smoke` | editor | input, actions, rebinding, `.winput`, Lua API / Input panel for 130 frames |
| `--audio-smoke` / `--audio-panel-smoke` | editor | audio module on the Null backend, soundbank asset and metadata, emitters and play-mode sync, settings, Lua API, event browser model, audition (and the Wwise backend in a `WWISESDK` build, with banks in `editor/assets/audio`) / Audio panel for 130 frames |
| `--ai-smoke` / `--ai-panel-smoke` | editor | behaviour tree format and validator, node semantics, blackboard, MoveTo on a baked navmesh, controllers, Lua leaves and the `AI` API, perception, the edit model, gizmo shapes / the tree editor in the Asset Editor for 120 frames. The editor exe often crashes at shutdown: read the log line |
| `--nav-smoke` / `--nav-panel-smoke` | editor | `.wnav` format, components and settings, Recast bake, Detour queries, crowd agents, Lua API and arrival hooks, the bake model (worker, stale check, cancel), overlay shapes, path tool / Navigation panel for 130 frames. The editor exe often crashes at shutdown: read the log line, not the exit code |
| `--anim-smoke` / `--anim-panel-smoke` | editor | skeleton import, pose math and nodes, Animator module, sockets, extraction, Lua API, animation graphs (file, state machine, blend spaces, layers, edit model) / Animation panel for 130 frames and the Graph editor on the demo warrior's graph |
| `--play-smoke` / `--demo-smoke` | editor | play mode / the demo scene end to end |
| `--render-test` | any | GPU pixel checks in `engine/src/renderer/test/*_checks.cpp`, run on the render thread |
| `--render-smoke <frames>` | any | render N frames then exit (1 if a validation message or error was logged) |
| `--scene-smoke-load` | game | load the `--scene` (or the pak's default scene) and quit |

Other flags: `--nav-bake <file>` (editor, with `--scene`: bakes the scene's first navmesh volume to the file), `--debug-draw` (game: a game starts with debug draw off), `--scene <path>`, `--pack <outdir>`, `--api=vulkan|d3d11|d3d12` (default Vulkan, no silent fallback), `--gpu-validation`, `--no-copy-queue`. Panel smokes run with ImGui asserts on.

## Architecture in one page

Modules (`IModule`, registered in `Engine`, [engine.cpp](engine/src/core/engine.cpp)), by update priority: `AssetManager` -100, `AssetPipelineModule` (editor) -90, `SceneModule` -80, `RenderModule` -50, `InputModule` -30, `PhysicsModule` -20, `UiModule` -10, `ScriptingModule` 0, `NavModule` 5, `AIModule` 6, `AnimationModule` 10, `AudioModule` 20, plus `Window` and the editor's `EditorModule`. Reach a module with `Engine::get_module`, except from the render and physics threads, which must never call it.

Rules that cut across areas:

- **Editor vs game.** Separate projects over one engine lib. No build-config macro (e.g. `WI_SHIPPING`) may decide editor-vs-game behaviour. Anything that imports or cooks (importers, `AssetDatabase`, packager, shader compilation) lives in `editor/src/asset_pipeline`; the engine has only the runtime.
- **Threads.** The scene, ECS, Lua, input and UI are main thread. The render thread and the physics thread each own their world and talk to main through plain-data commands in and immutable snapshots out. Off-thread work uses immutable data.
- **Assets are CPU-side only** and never touch NVRHI. Components and settings store `AssetRef`, never `Handle<T>`; only the renderer holds handles. `AssetType` is written to files: append before `Last`.
- **Component fields are described once** (`WI_COMPONENT`/`WI_FIELD`, `ui_fields()`) and feed serializer, inspector, overrides, clone, Lua and stubs. Add a field there, not in each consumer.
- **Nothing is compiled at run time.** HLSL in `editor/engine/shaders` is cooked to DXBC and SPIR-V by the editor; `#include`s are tracked dependencies.
- **Scripts and physics run only while the game clock plays.** Editor Play runs on a clone of the edit scene; Stop discards it.

| Area | Doc | Entry points |
| --- | --- | --- |
| Assets, packing, settings files | [assets.md](docs/superpowers/specs/assets.md) | `engine/src/assets`, `editor/src/asset_pipeline` |
| Scene, reflection, prefabs, loading | [scene.md](docs/superpowers/specs/scene.md) | `engine/src/scene` |
| Rendering, shaders, post, TAA, debug draw | [rendering.md](docs/superpowers/specs/rendering.md) | `engine/src/renderer`, `editor/engine/shaders` |
| Materials, surface shaders | [materials.md](docs/superpowers/specs/materials.md) | `.wmat`, `.wshader` |
| Lua scripting (user API: [docs/scripting.md](docs/scripting.md)) | [scripting.md](docs/superpowers/specs/scripting.md) | `engine/src/scripting` |
| Physics (Jolt) | [physics.md](docs/superpowers/specs/physics.md) | `engine/src/physics`, vendored `engine/vendor/JoltPhysics`; `JPH::` stays inside it |
| Animation (skeletons, GPU skinning, `Animator`, animation graphs `.wgraph`, Lua `Animation`) | [animation.md](docs/superpowers/specs/animation.md) | `engine/src/animation`, `editor/src/animation` |
| Offscreen views (previews, 3D UI widgets, readback; `ViewHost`, `ViewImage`) | [views.md](docs/superpowers/specs/views.md) | `engine/src/renderer/view`, `engine/src/scene/preview_scene.*` |
| Asset Editor, asset thumbnails (skeleton/mesh editors, sockets, joint rename) | [editor.md](docs/superpowers/specs/editor.md) | `editor/src/asset_editor` |
| Audio (Wwise behind a vendor-neutral backend, soundbanks, emitters) | [audio.md](docs/superpowers/specs/audio.md) | `engine/src/audio`, `engine/src/audio/wwise` (`AK::` stays inside it), `editor/src/audio` |
| Behaviour trees and perception (`.wbt`, `AIController`, Lua leaves `BtAction`/`BtCondition`, `Perceiver`, the tree editor) | [behavior-trees.md](docs/superpowers/specs/behavior-trees.md) | `engine/src/ai`, `editor/src/ai` |
| Navigation (Recast bake in the editor, Detour queries and DetourCrowd agents, `.wnav`, the `Nav` Lua API) | [navigation.md](docs/superpowers/specs/navigation.md) | `engine/src/nav`, `editor/src/asset_pipeline/nav_bake.*`, `editor/src/nav`; vendored `engine/vendor/recastnavigation`; `dt*` stays in `engine/src/nav`, `rc*` in the editor |
| Input | [input.md](docs/superpowers/specs/input.md) | `engine/src/input` |
| Game UI | [ui.md](docs/superpowers/specs/ui.md) | `engine/src/ui`, `editor/src/ui` |
| Project settings, Settings page | [project-settings.md](docs/superpowers/specs/project-settings.md) | `engine/src/core/project_settings.*`, `editor/src/settings` |
| Editor, play mode, gizmos, performance stats | [editor.md](docs/superpowers/specs/editor.md) | `editor/src` |

Animation in one line (the warrior of the demo plays `Warrior_animation/warrior.wgraph`, written by `scripts/gen_demo_graph.py`, run before `gen_demo_scene.py`): `AnimationModule` (after the scripts) evaluates a tree of pose nodes per `Animator` into a joint palette; palettes ride the scene delta to the render thread, which keeps the previous palette like `PrevWorld`, and vertex shaders skin through their own last binding set (`t22`/`t23`). The demo hall (`editor/assets/scenes/demo.wscene`) is written by `scripts/gen_demo_scene.py`, with CC0 FBX characters (the warrior's footstep events live in its `.wanim` clips, extracted from the model once, `<Model>_animation/`; the scene points at the GUIDs and legacy sub-asset ids in the models' `.meta` files, which redirect to the extracted assets, so the `.meta` files and the extracted files are force-added although `.meta` files are ignored).

Renderer in one line: the render thread owns all GPU state and a GUID-keyed `GpuResourceCache`, is fed per-frame deltas through a bounded queue, and runs Forward+ (shadows, depth/normal/velocity prepass, light culling, AO, forward, sky, TAA, motion blur, exposure, bloom, tonemap, FXAA, UI, outline, debug, blit, ImGui).

## Conventions

- `wiwa` namespace, tabs, `#include <pch.h>` first in engine files (`<editor_common.h>` in editor files), `Ref`/`Scope` smart pointers.
- Log with `WI_LOG` and its area variants (`WI_RENDER_LOG`, `WI_PHYSICS_LOG`, `WI_ASSET_LOG`...), never `std::cout`/`printf`.
- `near` and `far` are Windows macros: don't use them as identifiers.
- Premake only (no CMake). Third-party code is vendored under `engine/vendor/`; don't edit it except the patched NVRHI (rendering.md). The workspace `premake5.lua` still lists some stale `Wiwa/vendor/...` paths from an older project.
- Every new feature must be showcased in `editor/assets/scenes/demo.wscene` and covered by `--demo-smoke`.
- `docs/superpowers/specs/` is committed; `docs/superpowers/plans/` and `PROMPT.md` are git-ignored and never committed. Plans are scratch and may be stale.
