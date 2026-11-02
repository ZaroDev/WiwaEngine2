# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build and run

Windows only (VS 2022, x64, `VULKAN_SDK` must be set). Premake generates the solution; `*.sln`/`*.vcxproj` are git-ignored.

```bat
scripts\GenerateProjects2022.bat        :: runs vendor\premake\premake5.exe vs2022 -> Wiwa2.sln
msbuild Wiwa2.sln /p:Configuration=Debug /p:Platform=x64
```

Configurations: `Debug`, `Release`, `Shipping`. Output goes to `bin/<cfg>-windows-x86_64/<project>`. `scripts/Build*2022.bat` are stale (they reference `Wiwa.sln` and `Editor/tools/buildsln.bat`, which don't exist), so don't rely on them.

Three projects: `wiwa` (static lib, `engine/`), `wiwa_editor` (`editor/`, debugdir `editor/`) and `wiwa_game` (`game/`, debugdir `game/`). The game links only the engine, never editor code.

### Tests

There is no test framework (the user decided a test suite isn't needed). Verification is a set of smoke commands: run the executable with a flag, and the process exit code is the result (0 = pass).

| Flag | Where | Checks |
| --- | --- | --- |
| `--asset-smoke` | editor | import, pack and load round trip, GUID stability |
| `--scene-smoke` | editor | scene save, cook and load, prefab overrides |
| `--script-smoke` | editor | Lua script runtime |
| `--play-smoke` / `--demo-smoke` | editor | play mode / demo scene |
| `--render-test` | any (RenderModule) | GPU checks in `engine/src/renderer/test/*_checks.cpp`, run on the render thread |
| `--render-smoke <frames>` | any | render N frames, then exit |
| `--scene-smoke-load` | game | load the `--scene` and quit |

Other flags: `--scene <path>`, `--pack <outdir>`, `--api=vulkan|d3d11|d3d12` (default Vulkan; no silent fallback), `--gpu-validation`, `--no-copy-queue`. A smoke check is only trusted if it was seen to fail under a negative control.

## Architecture

Modules (`IModule`, registered in `Engine` in [engine.cpp](engine/src/core/engine.cpp)): `AssetManager`, `SceneModule`, `Window`, `RenderModule`, `ScriptingModule`. The editor adds `AssetPipelineModule` and `EditorModule`. Other code reaches a module through `Engine::get_module`, except the render thread, which must never call it.

**Editor vs game.** The editor and the game are separate projects over the same engine lib. No build-config macro (e.g. `WI_SHIPPING`) may decide editor-vs-game behaviour. Anything that imports or cooks assets (importers, `AssetDatabase`, packager, shader compilation) lives in `editor/src/asset_pipeline`; the engine only has the runtime.

**Assets** (`engine/src/assets`). Source files in `Assets/` have `.meta` sidecars with a GUID. The editor imports them into `Library/<xx>/<guid>.wasset` (disposable, git-ignored). A game reads `assets.wpak` (LZ4 pack) if it sits next to the program, otherwise `Library/` (`PakSource` vs `LibrarySource`). Assets are CPU-side only and never touch NVRHI. `AssetManager::load` is async and returns a `Handle<T>` (Loading/Ready/Failed). Models import as sub-assets (Mesh, Material, Prefab, ...) with stable `localId`s. **Components and settings store `AssetRef`, never `Handle<T>`**; only the renderer holds handles.

**Rendering** (`engine/src/renderer`). The renderer runs on its own thread, fed by immutable per-frame snapshots from the main thread through a bounded queue (`render_thread`, `render_snapshot.h`). The render thread owns all GPU resources and a GUID-keyed `GpuResourceCache`, and uploads on a per-frame byte budget (optionally via a dedicated copy queue). ImGui is built on the main thread and its draw data is copied into the frame. The backend interface sits in `renderer/backend` with Vulkan, D3D12 and D3D11 behind NVRHI. The Forward+ path is depth prepass, compute light culling, shadows, IBL, sky, then post (auto exposure, bloom, TAA, motion blur, tonemap, FXAA, GTAO).

**Shaders.** HLSL lives in `editor/engine/shaders` (user shaders in `Assets/`). The editor's shader importer cooks each into DXBC (D3D11/12) and SPIR-V (Vulkan, via DXC). Nothing is compiled at run time. `#include`d files are tracked as dependencies, so editing a `.hlsli` re-cooks everything that uses it.

**Scenes** (`engine/src/scene`). EnTT registry owned by `Scene`. Component fields are described once with the `WI_COMPONENT`/`WI_FIELD` reflection macros, which feed the serializer, inspector and prefab override engine through `ComponentRegistry`. There are two access backends: struct-offset (C++ components) and value-bag (Lua script properties). Scenes are `.wscene` assets (JSON source, cooked to binary). Prefab instances are saved as a root with overrides, and children are regenerated at load with deterministic entity GUIDs from stable node ids. Loading is async and time-sliced.

**Scripting** (`engine/src/scripting`, user docs in [docs/scripting.md](docs/scripting.md)). Sandboxed Lua via sol2, run on the main thread by `ScriptingModule`. Scripts run only while the game clock is playing. The editor's Play mode runs on a clone of the edit scene ([scene_clone.cpp](engine/src/scene/scene_clone.cpp)), and Stop discards it.

## Conventions

- `wiwa` namespace, tabs, `#include <pch.h>` first in engine files, `Ref`/`Scope` smart pointers.
- Log with `WI_LOG` and its per-area variants (e.g. `WI_RENDER_LOG`), never `std::cout`/`printf`.
- `near` and `far` are Windows macros, so avoid them as identifiers.
- Premake only (no CMake). Third-party code is vendored under `engine/vendor/`; the workspace `premake5.lua` still lists some stale dependency paths (`Wiwa/vendor/...`) from an older project.
- The scene and ECS live on the main thread. Work done off-thread uses immutable data.

## Design docs

`docs/superpowers/specs/` (design) and `docs/superpowers/plans/` (implementation plans) hold dated per-feature documents: asset system, renderer foundation, scene foundation, render path, environment and settings, post-processing, debug renderer and gizmos, build settings, TAA and motion blur, material assets and shaders, script runtime and editor play mode. Read the relevant spec before changing one of those areas, as the rationale and rejected alternatives are recorded there. Those docs are deliberately **not committed** to git, and neither is `PROMPT.md`.
