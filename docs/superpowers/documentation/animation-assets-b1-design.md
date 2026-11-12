# Native animation assets, part B1: formats, extraction, name binding

> Historical design record, written before the area was built. Its status line may be out of date. What exists now is described in animation.md; where the two differ, that doc is right.

Second of three sub-projects of the animation overhaul: **A** offscreen views (done, `views.md`), **B** standalone animation assets, **C** animation graph. B is split into **B1** (this doc: native formats, extraction from models, runtime name binding, redirects), **B2** (asset browser + thumbnails from A's readback + skeleton and skinned-mesh editors with previews) and **B3** (clip editor: asset-level operations, event timeline, dope sheet and curve editor for per-joint keys; candidate widgets from ImGuizmo's `ImSequencer`/`ImCurveEdit`, which means updating the vendored ImGuizmo, now v1.89 WIP in `editor/vendor`, to the latest; C's node editor can use its `GraphEditor`). After implementation, fold this into `animation.md` and `assets.md`.

## Decisions (from the owner)

- **Fully standalone authoring.** Skeleton, skinned mesh and clip are native files that need no model source.
- **Extract once, native files own the data.** Model import writes the files the first time and never overwrites them (the `_materials` rule).
- **Frame (key) editing** is in B's scope (B3).
- **Clips bind to joints by name** against any compatible skeleton; no retargeting.
- **Old references are redirected**, not migrated or duplicated.
- **Skinned meshes only** become `.wmesh`; static meshes stay model sub-assets.
- **Mixed formats:** `.wskel` JSON, `.wmesh` and `.wanim` binary.
- Verification: Release builds and manual checks. No smoke tests, no new render-test checks (owner's rule). Commit only when asked.

## Formats

- **`.wskel` (JSON):** `version`, `name`, `joints[]`. Each joint has `name` (unique), `parent` (index, -1 for a root; parents before children), rest pose `t`/`r`/`s`, `pre` (the transform between the joint and its parent joint, as today's `SkeletonData::Pre`) and `inverseBind` (16 floats). Joint order is a contract: meshes index into it. A tool may rename joints but must not reorder or delete them while meshes use it.
- **`.wmesh` (binary):** magic `WMSH`, version, chunks: vertices, indices, primitives (with default material refs), skin vertices, bounds, morph targets; the `skeleton` ref and the skeleton's joint-name list (mismatch check at cook time: on a mismatch the importer warns and the mesh draws rigid).
- **`.wanim` (binary):** magic `WANM`, version, name, duration, the `skeleton` ref it was authored on, channels by **joint name** (path, interpolation, times, values), events. A load rejects a bad magic or version cleanly, like `.wasset`.
- **Importers:** one per extension (`.wskel`, `.wmesh`, `.wanim`), in `editor/src/asset_pipeline/importers`, cooking to the existing `Skeleton`, `Mesh` and `AnimationClip` types. The shared read/write code for the native files lives in the engine lib (`engine/src/assets`), so the editors of B2/B3 write the same files the importers read. `AssetType` does not change.

## Runtime data

- `SkeletonData` drops `Joints`; the joint count is `JointNames.size()`; a name-to-index lookup is built at load (not serialized).
- `AnimationChannel::Node` becomes `std::string Joint`. `AnimationClipData` gains `AssetRef Skeleton`. `MeshData` gains `AssetRef Skeleton` and `SkinJointNames` (skinned meshes only).
- `WASSET_VERSION` goes to 3 (re-cook; an old pack fails to load cleanly).
- The importer's intermediate `ImportedModel` stays node-indexed; conversion to names happens only at extraction.
- `bind_clip(clip, skeleton)` maps each channel's joint name to a joint index, cached per (clip, skeleton) as today; channels with no joint are skipped with one warning per pair.
- `clip_compatibility(clip, skeleton)` returns Full / Partial (with the missing joint names) / None. Full and Partial play (Partial warns once); None is logged and treated as unusable. Editors use the same function for badges and assign-time checks.
- **Skinning stays by asset:** a skinned mesh is driven by the Animator whose `Skeleton` is the same asset (today's rule). `Animator`, `AnimationSocket`, `SkinBinding`, the Lua API (clips play by name), poses and GPU skinning do not change.
- The smoke-test fixtures (`editor/src/animation/anim_*`) are updated to the new shapes so the editor still compiles. They are not run.

## Extraction (model importer)

After the existing parsing and skeleton finalization, per model:

1. **Skeletons** to `<Model>_animation/<name>.wskel`. A duplicate joint name gets a `_2` suffix with a warning.
2. **Clips** to `<Model>_animation/clips/<name>.wanim`. Channels switch from node index to joint name through the skeleton the clip belongs to (with several skeletons, the one covering the most channels). Channels for nodes outside every skeleton, and morph-weight channels, are dropped with one warning per clip. The model's legacy `animationEvents` import setting is copied into the clip, at this point only.
3. **Skinned meshes** to `<Model>_animation/meshes/<name>.wmesh` with the skeleton ref. A mesh used under different skeletons takes the first, with a warning.
4. **Never overwrite, robust to renames.** The model's `.meta` records, per source item (`skeleton/0`, `clip/Walk`, `mesh/2`), the guid of its extracted asset. On re-import an item whose guid still exists is skipped (the file may have been renamed or moved); only items with no entry get new files (a clip added to the FBX). Removing a clip from the FBX leaves its file alone. A changed source clip never touches the native file; refreshing is an explicit "re-extract this asset" action in the B2/B3 editors.
5. **References:** the prefab points its skinned-mesh nodes, node skeletons and `Animations` list at the native assets, imported as dependencies with `ctx.import_dependency` (as materials are). The prefab expander is unchanged.
6. **Failure:** if a file cannot be written, the model import fails with a clear message (no silent fallback to legacy data).
7. Physics shapes are not generated for skinned meshes (their shape ids hang off the mesh's local id); the importer warns once when `generatePhysicsShapes` is on.
8. The existing Animation panel saves events to the clip's `.wanim` (read, replace the event list, write) instead of the model's `.meta`.
9. Extracted files and their `.meta` are project sources: committed, `.meta` with `git add -f`.

## Redirects

- `AssetCatalog` gains `Redirects: { fromGuid (resolved legacy id), toGuid }`. The editor's `write_catalog()` fills it from every model's extracted-item table; it is packed with the catalog.
- `AssetManager` applies it in `acquire`, `acquire_resolved`, `get_state` and `preload`. When the editor writes a catalog with new redirects, the manager re-reads it and notifies the old guids as reloaded (a scene loaded before the model's first extraction recovers).
- Legacy local ids (`skeleton/N`, `animation/N`, skinned `mesh/N`) stay reserved in the model's `.meta` and are never reused; they stop being cooked.
- The packer follows redirects when it walks dependencies and never looks for a cooked file at a legacy guid.
- "Find references" treats a legacy reference as a reference to its native asset. A menu action **Update references to extracted assets** rewrites `.wscene`, `.wprefab` and `.wuipanel` files to the native guids, after a dry run with counts and a confirm dialog. It is optional, since redirects already work.

## Demo hall

The scene keeps legacy references and works through the redirects. The first editor import of the character models extracts the files and seeds the Warrior's footstep events from its legacy `animationEvents`; the extracted files and their `.meta` are then committed. `gen_demo_scene.py` keeps writing the legacy settings as the extraction seed (its docstring says so). Entity names, clip names and script behaviour the demo depends on do not change.

## Verification (manual, Release)

Import the Warrior: the `.wskel`, `.wanim` and `.wmesh` files appear. Play the demo: the animated Warrior and its footsteps still work. Edit an event in the Animation panel: it lands in the `.wanim`. Re-import: nothing is overwritten. Rename an extracted file and re-import: no duplicate. Load a scene that names legacy ids on a fresh Library: it recovers. Export a pack and run the game on it. A partially compatible clip warns once and plays the matching joints.

## Out of scope for B1

Any new editor UI beyond the Animation panel's event save and the update-references action (B2, B3); thumbnails (B2); keyframe editing (B3); static `.wmesh`; retargeting; sockets in `.wskel` (B2 adds them with a version bump); animating offscreen views (needed by B2/B3 previews: `AnimationModule` must hold several scenes first, a B2 prerequisite).
