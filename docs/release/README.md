# Wiwa Engine 2 - v1.0

The first release of Wiwa Engine 2: a C++20 game engine and scene editor for Windows, built around a multithreaded
Forward+ renderer. It ships with a demo hall that exercises every system.

![The Wiwa2 editor](editor.png)

![The demo hall running in wiwa_game](demo-hall.png)

## What is in 1.0

- **Renderer.** Render thread fed with immutable per-frame snapshots and a dedicated copy queue. Forward+ PBR with
  shadows, image-based lighting, a physical sky and atmosphere, GPU skinning, projected decals and GPU particles.
  Post-processing: auto exposure, bloom, tonemapping, TAA, motion blur, FXAA, GTAO.
- **Graphics APIs.** Vulkan, Direct3D 12 and Direct3D 11 behind one backend interface.
- **Assets.** GUID-based pipeline with hot-reloading importers (textures, glTF/FBX, shaders, materials, scenes, audio,
  video, particle effects) and a packager for shipping builds.
- **Scenes.** `.wscene` assets with nestable prefab instances and reflection-driven serialisation.
- **Scripting.** Sandboxed Lua with hot reload and LuaLS IntelliSense.
- **Gameplay.** Jolt physics (joints, character controller), skeletal animation with animation graphs, audio (built-in
  backend or Wwise), navigation meshes (Recast/Detour), behaviour trees with perception, input actions with rebinding,
  game UI, video playback, decals and VFX.
- **Editor.** Dockable viewport, hierarchy, inspector, asset browser and console; gizmos, GPU picking, undo/redo, play
  mode on a scene clone; editors for animation clips and graphs, particle effects, behaviour trees and game UI.

## Download contents

| Item | Description |
| --- | --- |
| `wiwa_editor.exe` | The scene editor (run it with `editor/` as the working directory) |
| `wiwa_game.exe` + `assets.wpak` | The runtime, packaged with the demo hall |
| `editor/` | The demo project's assets and engine shaders |

## Requirements

- Windows 10/11 x64.
- A GPU and current drivers for Vulkan, Direct3D 12 or Direct3D 11. Vulkan is the default.
- To build from source: Visual Studio 2022 (C++ desktop workload) and the Vulkan SDK with `VULKAN_SDK` set. See the
  main [README](../../README.md).

## Try the demo hall

- **Game:** run `wiwa_game.exe`. Keys `1`-`7` jump between the stations (lighting, materials, animation, physics,
  joints, character controller, scripts/input/UI).
- **Editor:** run `wiwa_editor.exe --scene assets/scenes/demo.wscene` from `editor/`, then press Play.

## Command line

| Flag | Description |
| --- | --- |
| `--scene <path>` | Open (editor) or load (game) a scene on startup |
| `--pack <outdir>` | Editor: package assets for distribution |
| `--api=vulkan\|d3d11\|d3d12` | Graphics API (default Vulkan, no silent fallback) |
| `--gpu-validation` | Enable graphics API validation layers |
| `--no-copy-queue` | Disable the dedicated copy queue |

If an executable does not start, try another `--api` and read `wiwa.log` next to it.

## Known limitations

- Windows only.
- Wwise audio needs a build with the Wwise SDK (`WWISESDK`); the download uses the built-in backend.
- The demo uses CC0 character models; their licenses are in `editor/assets/models/`.

## Documentation

Scripting API: [docs/scripting.md](../scripting.md). Per-area engine docs:
[docs/superpowers/documentation](../superpowers/documentation/README.md).
