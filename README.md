# Wiwa2

A C++20 game engine and scene editor for Windows, built around a multithreaded Forward+ renderer.

![Wiwa2 editor](docs/images/editor.png)

## Highlights

- **Render thread.** The renderer runs on its own thread and is fed immutable per-frame snapshots, with uploads
  handled by a dedicated copy queue.
- **Forward+ PBR pipeline.** Depth prepass, compute light culling, shadow mapping, image-based lighting, a
  physically based sky and atmosphere, GPU skinning, projected decals and GPU particles.
- **Post-processing.** Auto exposure, bloom, tonemapping, TAA, motion blur, FXAA and GTAO.
- **Multiple graphics APIs.** Vulkan, Direct3D 12 and Direct3D 11 behind one backend interface.
- **GUID-based asset pipeline.** Hot-reloading importers for textures, glTF/FBX models, shaders, materials, scenes,
  audio, particle effects and more, with a packager for shipping builds.
- **Scene system.** Scenes are `.wscene` assets with linked, nestable prefab instances and reflection-driven
  serialisation.
- **Lua scripting.** Sandboxed scripts with hot reload, a documented API ([docs/scripting.md](docs/scripting.md)) and
  LuaLS IntelliSense.
- **Gameplay systems.** Jolt physics (joints, character controller), skeletal animation with animation graphs, audio
  (a built-in backend or Wwise), navigation meshes (Recast/Detour), behaviour trees with perception, input actions
  and rebinding, and a game UI.
- **Editor.** Dockable viewport, hierarchy, inspector, asset browser and console, with transform gizmos, GPU picking,
  selection outlines, undo/redo, play mode on a scene clone, and editors for animation clips and graphs, particle
  effects and behaviour trees.

## Building

**Requirements:** Windows 10/11 x64, Visual Studio 2022 (C++ desktop workload), and the
[Vulkan SDK](https://vulkan.lunarg.com/) with `VULKAN_SDK` set. Wwise is optional (`WWISESDK`).

Generate the Visual Studio solution with premake from the repository root, then build:

```bat
vendor\premake\premake5.exe vs2022
msbuild Wiwa2.sln /p:Configuration=Release /p:Platform=x64
```

Re-run the generator after adding or deleting files. Output goes to `bin/<configuration>-windows-x86_64/<project>`.

## Usage

Launch `wiwa_editor` from Visual Studio or with `editor/` as the working directory.

| Flag | Description |
| --- | --- |
| `--scene <path>` | Open a scene on startup |
| `--pack <outdir>` | Package assets for distribution |
| `--api=vulkan\|d3d11\|d3d12` | Choose the graphics API (default Vulkan, no silent fallback) |
| `--gpu-validation` | Enable graphics API validation layers |
| `--no-copy-queue` | Disable the dedicated copy queue |
| `--nav-bake <file>` | With `--scene`: bake the scene's first navmesh volume to the file and exit |

## Tests

Every check lives in the `wiwa_tests` project. Run it from `editor/` with one flag; the exit code is the result
(0 = passed), for example `..\bin\Release-windows-x86_64\wiwa_tests\wiwa_tests.exe --scene-smoke`. The flags
(`--asset-smoke`, `--script-smoke`, `--render-test`, ...) are listed in [CLAUDE.md](CLAUDE.md).

## Project structure

```
engine/    wiwa static library: core, assets, renderer, scene, physics, animation, audio, nav, ai, input, ui, scripting
editor/    wiwa_editor editor application (asset pipeline, panels, asset editors)
game/      wiwa_game runtime application (links the engine only)
tests/     wiwa_tests: every smoke and render check
scripts/   project generation and the demo scene generators
docs/      scripting API (docs/scripting.md) and per-area design notes (docs/superpowers/documentation)
```

## Dependencies

- [GLFW](https://github.com/glfw/glfw)
- [NVRHI](https://github.com/NVIDIA-RTX/NVRHI)
- [vk-bootstrap](https://github.com/charles-lunarg/vk-bootstrap)
- [Dear ImGui](https://github.com/ocornut/imgui)
- [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo)
- [EnTT](https://github.com/skypjack/entt)
- [GLM](https://github.com/g-truc/glm)
- [spdlog](https://github.com/gabime/spdlog)
- [nlohmann/json](https://github.com/nlohmann/json)
- [stb](https://github.com/nothings/stb)
- [cgltf](https://github.com/jkuhlmann/cgltf)
- [ufbx](https://github.com/ufbx/ufbx)
- [LZ4](https://github.com/lz4/lz4)
- [filewatch](https://github.com/ThomasMonkman/filewatch)
- [Jolt Physics](https://github.com/jrouwe/JoltPhysics)
- [Lua](https://www.lua.org/) and [sol2](https://github.com/ThePhD/sol2)
- [Recast & Detour](https://github.com/recastnavigation/recastnavigation)
- [miniaudio](https://github.com/mackron/miniaudio)
