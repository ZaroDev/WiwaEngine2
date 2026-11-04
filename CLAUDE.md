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
| `--play-smoke` / `--demo-smoke` | editor | play mode / the demo scene end to end |
| `--render-test` | any | GPU pixel checks in `engine/src/renderer/test/*_checks.cpp`, run on the render thread |
| `--render-smoke <frames>` | any | render N frames then exit (1 if a validation message or error was logged) |
| `--scene-smoke-load` | game | load the `--scene` (or the pak's default scene) and quit |

Other flags: `--debug-draw` (game: a game starts with debug draw off), `--scene <path>`, `--pack <outdir>`, `--api=vulkan|d3d11|d3d12` (default Vulkan, no silent fallback), `--gpu-validation`, `--no-copy-queue`. Panel smokes run with ImGui asserts on.

## Architecture in one page

Modules (`IModule`, registered in `Engine`, [engine.cpp](engine/src/core/engine.cpp)), by update priority: `AssetManager` -100, `AssetPipelineModule` (editor) -90, `SceneModule` -80, `RenderModule` -50, `InputModule` -30, `PhysicsModule` -20, `UiModule` -10, `ScriptingModule` 0, plus `Window` and the editor's `EditorModule`. Reach a module with `Engine::get_module`, except from the render and physics threads, which must never call it.

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
| Input | [input.md](docs/superpowers/specs/input.md) | `engine/src/input` |
| Game UI | [ui.md](docs/superpowers/specs/ui.md) | `engine/src/ui`, `editor/src/ui` |
| Editor, play mode, gizmos, performance stats | [editor.md](docs/superpowers/specs/editor.md) | `editor/src` |

Renderer in one line: the render thread owns all GPU state and a GUID-keyed `GpuResourceCache`, is fed per-frame deltas through a bounded queue, and runs Forward+ (shadows, depth/normal/velocity prepass, light culling, AO, forward, sky, TAA, motion blur, exposure, bloom, tonemap, FXAA, UI, outline, debug, blit, ImGui).

## Conventions

- `wiwa` namespace, tabs, `#include <pch.h>` first in engine files (`<editor_common.h>` in editor files), `Ref`/`Scope` smart pointers.
- Log with `WI_LOG` and its area variants (`WI_RENDER_LOG`, `WI_PHYSICS_LOG`, `WI_ASSET_LOG`...), never `std::cout`/`printf`.
- `near` and `far` are Windows macros: don't use them as identifiers.
- Premake only (no CMake). Third-party code is vendored under `engine/vendor/`; don't edit it except the patched NVRHI (rendering.md). The workspace `premake5.lua` still lists some stale `Wiwa/vendor/...` paths from an older project.
- Every new feature must be showcased in `editor/assets/scenes/demo.wscene` and covered by `--demo-smoke`.
- `docs/superpowers/specs/` is committed; `docs/superpowers/plans/` and `PROMPT.md` are git-ignored and never committed. Plans are scratch and may be stale.
