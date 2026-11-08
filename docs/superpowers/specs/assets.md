# Assets

Code: `engine/src/assets` (runtime), `editor/src/asset_pipeline` (import, cook, pack). Verified by `--asset-smoke`.

## Model

- Source files live in `Assets/` (editor: `editor/assets/`) with a `<file>.meta` JSON sidecar: `guid`, `importer`, `version`, `importSettings`, and for models a stable `localId` per sub-asset. Moving a file together with its `.meta` keeps the GUID. `*.meta` is git-ignored in this repo.
- The editor imports sources into `Library/<xx>/<guid>.wasset` (sharded by the first two hex digits). Header: magic, format version, asset type, GUID, dependency GUIDs, payload size; then a typed binary payload. `Library/import_cache.json` holds content and settings hashes so only changed sources re-import. `Library/` is disposable.
- Engine-owned assets (shaders, fonts, the Lua stub) live in `editor/engine/` and are cooked like user assets (the `engine` root).
- `AssetRef` = GUID + `localId`. `Handle<T>` is what `AssetManager::load` returns (Loading / Ready / Failed). **Components and settings store `AssetRef` only**; only the render thread holds handles.
- Asset data is CPU-side only and immutable once Ready. GPU objects are made by the renderer (see rendering.md). Assets never touch NVRHI.
- `WASSET_VERSION` is 5 (animation graphs; 4: skeletons carry sockets, clips drive joints by name, clips and skinned meshes name their skeleton, skeletons have no node indices). A bump re-cooks everything: the import cache stores the version it was cooked with. Details of skeletons and clips: animation.md.
- `AssetType` is written to files: append new types before `Last`, never reorder. Current types: Texture, Mesh, Material, Skeleton, AnimationClip, Prefab, Environment, Lut, Shader, Scene, Script, SurfaceShader, Font, UiPanel, PhysicsMaterial, PhysicsShape, InputMap, AnimationGraph.

## Editor vs game

The editor and the game are separate projects over the same engine lib. Anything that imports or cooks (importers, `AssetDatabase`, `Packager`, shader compilation, DXC) lives in the editor project; the game only has the runtime. No build-config macro (`WI_SHIPPING`) may decide editor-vs-game behaviour.

## Loading

`AssetManager` (module priority -100, thread-safe) loads on a worker pool through an `IAssetSource`: `PakSource` if `assets.wpak` sits next to the program, otherwise `LibrarySource`. Dependencies load recursively; `preload()` takes a list so a scene's content loads in parallel. A reload listener (called on the importer's thread) lets the renderer hot-reload; it must only queue the GUID.

A failed load gives a Failed handle and a log line; consumers substitute fallbacks (white texture, magenta material, skipped mesh). A format-version mismatch is rejected on load and the editor re-imports.

## Importing

- `AssetDatabase` scans on the worker pool, watches files (`filewatch`) with debounce and re-imports. A failed import logs, keeps the previous cooked asset and marks the asset Failed. Cooked files are written atomically (temp, then rename).
- `ImportContext::add_source_dependency` records extra files an importer read (e.g. `.hlsli` includes); the up-to-date check includes their hashes.
- Importers (`editor/src/asset_pipeline/importers`): model (glTF via cgltf, FBX via ufbx; sub-assets Mesh (static meshes), Prefab, optional physics shapes; **skeletons, clips and skinned meshes are extracted once into `.wskel`/`.wanim`/`.wmesh` files** next to the model, see animation.md), the native skeleton/clip/mesh files, the animation graph (`.wgraph`, `graph_importer.cpp`; a graph that breaks `graph_valid` fails the import and keeps the old cooked one), texture (PNG/JPG/HDR via stb; `.hdr` also cooks the environment IBL), `.cube` LUT, shader (`.hlsl`/`.hlsli`), surface shader (`.wshader`), material (`.wmat`), scene (`.wscene`), prefab (`.wprefab`), script (`.lua`), font, UI panel, physics material, input map. Textures use raw mips with LZ4 in the pack; no BC compression yet.
- Model import extracts materials to `<ModelDir>/<ModelName>_materials/*.wmat` (never overwrites; see materials.md) and animation assets to `<ModelDir>/<ModelName>_animation/` by the same rule. What it moved is recorded in the model's `.meta` (`extracted`), and the sub-assets it used to produce are **redirected** to the new assets through the catalog (`AssetCatalog::Redirects`, followed by `AssetManager` and the packer). Prefab nodes carry components (cooked prefab has a trailing component section so older prefabs still load); `.wprefab` is the `.wscene` JSON of a subtree.
- `.lua` files whose first line starts with `---@meta` are editor support stubs and are skipped.
- Stable prefab node ids: a hash of the node's name path, suffixed when siblings share a name. Overrides address nodes by this id.

## Packing and build settings

- `Packager` brings `Library/` up to date, then writes `assets.wpak`: header, GUID index (offset, size, type, content hash), LZ4 blobs. Triggered by the editor's **Export** button (blocking progress popup, output `../game/`) and by `--pack <outdir>`.
- `editor/build_settings.json` (not an asset): `{ defaultScene, scenes[], alwaysInclude[] }`, all GUIDs. Missing file = pack everything, no default scene. `defaultScene` must be in `scenes` or packing fails.
- Roots = `scenes` + `alwaysInclude` + every engine asset + the settings assets below. The packer walks `Dependencies` transitively; what is not reached is not packed (no per-asset "never include"). The catalog in the pak is filtered to packed assets.
- Reserved-GUID pak entries: the catalog (`CATALOG_GUID`), build info (holds the default scene guid), `PHYSICS_SETTINGS_GUID`, `INPUT_SETTINGS_GUID`. The packager always packs the `.winput` named by `input.wsettings`.
- A game started without `--scene` loads the build-info default scene when its source is a `PakSource` (`SceneModule::init`); `--scene` overrides.
- The Build Settings panel is a popup (scene list with include/default, always-include list fed by the browser's context menu).

## Thumbnails and the editor state (`editor/src/asset_editor`)

- Skeleton, clip and skinned-mesh files show a **thumbnail** in the Asset Browser (`ThumbnailCache`). It is made the first time a cell is visible and kept as `Library/thumbnails/<guid>-<key>.png`; the key is a hash of the file, of the skeleton and preview mesh files it was made from, of `ThumbnailCache::VERSION` and of `WASSET_VERSION`, so editing or re-importing any of them makes a new one (the old PNGs of that guid are deleted). A later run only decodes PNGs. Materials are not part of the key. Over 512 MB the oldest PNGs go at start-up; at most 512 thumbnail textures stay on the GPU.
- The `RenderModule::create_ui_image` / `release_ui_image` pair uploads a small RGBA image the UI can draw; the cache is its first user.
- `Library/editor_state.json` keeps what the editor remembers outside the assets: the mesh a skeleton is previewed with. A damaged file is an empty state.
- A skeleton's meshes and clips are found by `SkeletonIndex`: one background scan of every `.wmesh`/`.wanim` under the source roots, started again whenever the catalog changes.

## Vendored ImGuizmo (`editor/vendor/ImGuizmo`)

Upstream commit 105e4fd (10 Oct 2026), version 1.92.5 WIP (see its README.txt). Only `ImGuizmo.h/.cpp` (gizmo) and `ImCurveEdit.h/.cpp` (curves of the clip editor) are compiled; the other files (`ImSequencer`, `GraphEditor` for the animation graph, `ImGradient`, ...) are there but not in `editor/premake5.lua`.

## Project settings files (editor working directory)

Only `project.wproject` (under `PROJECT_SETTINGS_GUID`), `physics.wsettings` and `input.wsettings` are packed into `assets.wpak`. `render_quality.json` is read from next to the program.

| File | Content | Edited in |
| --- | --- | --- |
| `build_settings.json` | scenes, default scene | Build Settings popup |
| `render_quality.json` | `RenderQuality` (rendering.md) | Settings page, Renderer |
| `project.wproject` | name, version, window, launch, performance ([project-settings.md](project-settings.md)) | Settings page, Project / Launch / Performance |
| `physics.wsettings` | layers and collision matrix | Settings page, Physics |
| `input.wsettings` | default `.winput` asset | Settings page, Input |
| `gizmo_settings.json` | editor gizmo toggles (editor only) | Viewport Gizmos menu, Settings page, Editor |

A missing or unreadable file gives defaults, logged once.

- **Dropped references.** An importer keeps only the references that exist (`ImportContext::asset_exists`). The ones it dropped are stored as `unresolved` in the import cache; a file is cooked again once one of them exists (`AssetDatabase::refresh_unresolved`, run at the end of a scan and after `reimport`). Without this a scene cooked before its HUD panel was known (imports run in parallel) kept a dependency list without it, and the pack shipped without the panel.
