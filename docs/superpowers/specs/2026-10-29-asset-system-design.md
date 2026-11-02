# Asset System Design

Date: 2026-10-29
Status: Draft, awaiting review
Part of: NVRHI renderer integration (sub-project 1 of 2; sub-project 2 is the renderer port, render thread, EnTT components and ImGui panels, specified separately).

## Goal

A Unity-style asset pipeline for the Wiwa engine: source assets in a user folder with `.meta` sidecars, imported into a `Library/` cache in the engine's own binary formats, addressed at runtime by GUID handle, and packable into a single archive for shipping games. The renderer integration (sub-project 2) builds on it.

## Decisions (from brainstorming)

- Handle-based manager; assets are addressed by GUID, ECS components store handles.
- Models import as Unity-style sub-assets (Mesh, Material, Skeleton, AnimationClip, Prefab), each with a GUID plus stable local ID.
- Imports run at editor startup (scan) and live (file watch). A game never imports: it only has the runtime.
- Packaging contains everything in `Library/`, produced from an editor menu action and a CLI flag (`--pack <outdir>`).
- Asset data is CPU-side only. GPU resources are created by the renderer on its own thread. Assets never touch NVRHI.

## On-disk layout

- `Assets/`: source files plus `<file>.meta` sidecars. A meta file is JSON: `guid`, `importer`, `version`, `importSettings`, and for models a stable `localId` per sub-asset so re-imports keep GUIDs. Moving or renaming a file together with its `.meta` preserves the GUID.
- `Library/`: one `.wasset` per GUID, sharded as `Library/ab/ab12....wasset`. Header: magic, format version, asset type, GUID, dependency GUIDs, payload size; then a typed binary payload. `Library/import_cache.json` records each source's content hash and import-settings hash so only changed sources are re-imported. `Library/` is disposable and git-ignored.
- `assets.wpak`: header, GUID index (offset, size, type, content hash), then LZ4-compressed `.wasset` blobs. Read by a game instead of `Library/` when it sits next to the program.

## Code layout (`engine/src/assets/`)

- `AssetGuid` (128-bit) and `AssetRef` (guid + local sub-asset ID). `Handle<T>` is the type ECS components hold.
- Asset types: Texture, Mesh, Material, Skeleton, AnimationClip, Prefab (node hierarchy referencing the others by GUID), Environment (HDR), Lut, Shader.
- CPU data types (`MeshData`, `TextureData`, ...), adapted from the renderer's existing `ModelData` and `ImageLoader` output.
- `IAssetSource` with `LibrarySource` (loose files) and `PakSource` (the pack).
- `AssetManager` (`IModule`): async `load(ref)` on a worker pool, refcounted cache, lazy eviction of unused assets. The same API in editor and game; the source is `PakSource` if an `assets.wpak` is next to the program, else `LibrarySource`. The build configuration plays no part.
- Editor project only (the game and the editor are separate projects built on the engine library, in any configuration): `AssetDatabase` (scan, GUID<->path map, `filewatch` watcher), `IImporter` per format, `Packager`.
- Importers: glTF (cgltf), FBX (ufbx), PNG/JPG/HDR (stb), `.cube` LUT, `.hlsl` shader. Third-party code is vendored under `engine/vendor/` with premake.
- Worker pool: the renderer's `JobSystem` is adapted into the engine. It must not touch the GPU.

## Data flow

### Import (editor)

1. `AssetDatabase` scans `Assets/` on the worker pool. A file without a `.meta` gets a new GUID and a default `.meta`.
2. The importer for the file's extension returns one or more cooked assets. A model returns a Prefab, meshes, materials, a skeleton and clips, each with a stable `localId`. Its textures are imported as separate Texture assets because they are separate source files.
3. Each cooked asset is written to `Library/` atomically (temp file, then rename). Dependency GUIDs go in the header.
4. The watcher debounces changes and queues a re-import. After a successful import it emits `AssetReimported(guid)`, which a later renderer pass can use for hot reload.

### Load (runtime)

`AssetManager::load<T>(ref)` returns a `Handle<T>` immediately in state Loading, Ready or Failed. A worker reads the blob from `IAssetSource`, deserializes it, and loads its dependencies recursively.

### Packaging

`Packager` brings the Library up to date, then writes `assets.wpak` containing all cooked assets with a content hash per entry. Triggered by an editor menu item and by `--pack <outdir>`.

## Error handling

- Failed import: logged via `WI_ERROR("Assets", ...)`, the previous cooked asset is kept if one exists, and the asset is marked Failed in the database.
- Failed load: a Failed handle and a log line. The renderer substitutes fallbacks (white texture, placeholder cube).
- Format version mismatch: rejected on load; the editor re-imports.
- Logging uses the engine's `WI_LOG` conventions, never `std::cout`.

## Out of scope

- Scene serialization.
- Dependency-based stripping of packs (needs scenes).
- Texture compression (BC7) and mip streaming; raw mips with LZ4 for now.
- Shader cooking; shaders stay source-plus-compile-at-load until the renderer spec.
- Asset browser UI (arrives with the renderer/editor work).

## Verification

The engine has no test framework and the user has said a test suite is not needed. Verification is a small editor-side smoke command that runs an import, pack and load round trip against sample assets, and checks that GUIDs are stable across re-import.
