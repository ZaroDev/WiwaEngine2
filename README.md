# Wiwa2

A C++20 game engine and scene editor for Windows, built around a multithreaded Forward+ renderer.

![Wiwa2 editor](docs/images/editor.png)

## Highlights

- **Render thread.** The renderer runs on its own thread and is fed immutable per-frame snapshots, with uploads
  handled by a dedicated copy queue.
- **Forward+ PBR pipeline.** Depth prepass, compute light culling, shadow mapping, image-based lighting, and a
  physically based sky and atmosphere.
- **Post-processing.** Auto exposure, bloom, tonemapping, FXAA and GTAO.
- **Multiple graphics APIs.** Vulkan, Direct3D 12 and Direct3D 11 behind one backend interface.
- **GUID-based asset pipeline.** Hot-reloading importers for textures, glTF/FBX models, shaders and scenes, with a
  packager for shipping builds.
- **Scene system.** Scenes are `.wscene` assets with linked prefab instances and reflection-driven serialisation.
- **Editor.** Dockable viewport, hierarchy, inspector, asset browser and console, with transform gizmos, GPU picking
  and selection outlines.

## Building

**Requirements:** Windows 10/11 x64, Visual Studio 2022 (C++ desktop workload), and the
[Vulkan SDK](https://vulkan.lunarg.com/) with `VULKAN_SDK` set.

Generate the Visual Studio solution with the script in `scripts/`:

```bat
scripts\GenerateProjects2022.bat
```

## Usage

Launch `wiwa_editor` from Visual Studio or with `editor/` as the working directory.

| Flag | Description |
| --- | --- |
| `--scene <path>` | Open a scene on startup |
| `--pack` | Package assets for distribution |
| `--gpu-validation` | Enable graphics API validation layers |
| `--no-copy-queue` | Disable the dedicated copy queue |

## Project structure

```
engine/    wiwa static library: core, assets, renderer, scene, math, platform
editor/    wiwa_editor editor application
game/      wiwa_game runtime appication
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
