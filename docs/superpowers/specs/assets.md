# Assets

Code: `engine/src/assets` (runtime), `editor/src/asset_pipeline` (import, cook, pack). Verified by `--asset-smoke`.

## Model

- Source files live in `Assets/` (editor: `editor/assets/`) with a `<file>.meta` JSON sidecar: `guid`, `importer`, `version`, `importSettings`, and for models a stable `localId` per sub-asset. Moving a file together with its `.meta` keeps the GUID. `*.meta` is git-ignored in this repo.
- The editor imports sources into `Library/<xx>/<guid>.wasset` (sharded by the first two hex digits). Header: magic, format version, asset type, GUID, dependency GUIDs, payload size; then a typed binary payload. `Library/import_cache.json` holds content and settings hashes so only changed sources re-import. `Library/` is disposable.
- Engine-owned assets (shaders, fonts, the Lua stub) live in `editor/engine/` and are cooked like user assets (the `engine` root).
- `AssetRef` = GUID + `localId`. `Handle<T>` is what `AssetManager::load` returns (Loading / Ready / Failed). **Components and settings store `AssetRef` only**; only the render thread holds handles.
- Asset data is CPU-side only and immutable once Ready. GPU objects are made by the renderer (see rendering.md). Assets never touch NVRHI.
- `AssetType` is written to files: append new types before `Last`, never reorder. Current types: Texture, Mesh, Material, Skeleton, AnimationClip, Prefab, Environment, Lut, Shader, Scene, Script, SurfaceShader, Font, UiPanel, PhysicsMaterial, PhysicsShape, InputMap.

## Editor vs game

The editor and the game are separate projects over the same engine lib. Anything that imports or cooks (importers, `AssetDatabase`, `Packager`, shader compilation, DXC) lives in the editor project; the game only has the runtime. No build-config macro (`WI_SHIPPING`) may decide editor-vs-game behaviour.

## Loading

`AssetManager` (module priority -100, thread-safe) loads on a worker pool through an `IAssetSource`: `PakSource` if `assets.wpak` sits next to the program, otherwise `LibrarySource`. Dependencies load recursively; `preload()` takes a list so a scene's content loads in parallel. A reload listener (called on the importer's thread) lets the renderer hot-reload; it must only queue the GUID.

A failed load gives a Failed handle and a log line; consumers substitute fallbacks (white texture, magenta material, skipped mesh). A format-version mismatch is rejected on load and the editor re-imports.

## Importing

- `AssetDatabase` scans on the worker pool, watches files (`filewatch`) with debounce and re-imports. A failed import logs, keeps the previous cooked asset and marks the asset Failed. Cooked files are written atomically (temp, then rename).
- `ImportContext::add_source_dependency` records extra files an importer read (e.g. `.hlsli` includes); the up-to-date check includes their hashes.
- Importers (`editor/src/asset_pipeline/importers`): model (glTF via cgltf, FBX via ufbx; sub-assets Mesh, Material, Prefab, Skeleton, Clip, optional physics shapes), texture (PNG/JPG/HDR via stb; `.hdr` also cooks the environment IBL), `.cube` LUT, shader (`.hlsl`/`.hlsli`), surface shader (`.wshader`), material (`.wmat`), scene (`.wscene`), prefab (`.wprefab`), script (`.lua`), font, UI panel, physics material, input map. Textures use raw mips with LZ4 in the pack; no BC compression yet.
- Model import extracts materials to `<ModelDir>/<ModelName>_materials/*.wmat` (never overwrites; see materials.md). Prefab nodes carry components (cooked prefab has a trailing component section so older prefabs still load); `.wprefab` is the `.wscene` JSON of a subtree.
- `.lua` files whose first line starts with `---@meta` are editor support stubs and are skipped.
- Stable prefab node ids: a hash of the node's name path, suffixed when siblings share a name. Overrides address nodes by this id.

## Packing and build settings

- `Packager` brings `Library/` up to date, then writes `assets.wpak`: header, GUID index (offset, size, type, content hash), LZ4 blobs. Triggered by the editor's **Export** button (blocking progress popup, output `../game/`) and by `--pack <outdir>`.
- `editor/build_settings.json` (not an asset): `{ defaultScene, scenes[], alwaysInclude[] }`, all GUIDs. Missing file = pack everything, no default scene. `defaultScene` must be in `scenes` or packing fails.
- Roots = `scenes` + `alwaysInclude` + every engine asset + the settings assets below. The packer walks `Dependencies` transitively; what is not reached is not packed (no per-asset "never include"). The catalog in the pak is filtered to packed assets.
- Reserved-GUID pak entries: the catalog (`CATALOG_GUID`), build info (holds the default scene guid), `PHYSICS_SETTINGS_GUID`, `INPUT_SETTINGS_GUID`. The packager always packs the `.winput` named by `input.wsettings`.
- A game started without `--scene` loads the build-info default scene when its source is a `PakSource` (`SceneModule::init`); `--scene` overrides.
- The Build Settings panel is a popup (scene list with include/default, always-include list fed by the browser's context menu).

## Project settings files (editor working directory)

Only `physics.wsettings` and `input.wsettings` are packed into `assets.wpak`. `render_quality.json` is read from next to the program.

| File | Content | Edited in |
| --- | --- | --- |
| `build_settings.json` | scenes, default scene | Build Settings popup |
| `render_quality.json` | `RenderQuality` (rendering.md) | World panel, Quality section |
| `physics.wsettings` | layers and collision matrix | Physics panel |
| `input.wsettings` | default `.winput` asset | Input panel |
| `gizmo_settings.json` | editor gizmo toggles (editor only) | Viewport Gizmos menu |

A missing or unreadable file gives defaults, logged once.
