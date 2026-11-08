# Animation assets B2: browser, thumbnails, Asset Editor

Status: design, approved in conversation 2026-11-07. Builds on A (offscreen views, `views.md`) and B1 (native `.wskel`/`.wanim`/`.wmesh`, `animation.md`). B3 (clip editor) and C (animation graph) follow and plug into what is built here.

## Goal

Skeletons, clips and skinned meshes show up in the asset browser with thumbnails, and open in one **Asset Editor** panel with a live preview. The skeleton and mesh editors inspect the asset and make safe edits (rename joints, sockets, material slots, skeleton rebind).

## Decisions (from the brainstorm)

| Question | Decision |
| --- | --- |
| How the editors present | One dockable **Asset Editor** panel, content by selected asset type; preview viewport left, type-specific tools right. One `IAssetEditor` per type; B3 adds `ClipEditor`. |
| Thumbnails | **Lazy, cached on disk** in `Library/thumbnails/`; rendered on first sight in the browser. Not at import time (importers only cook). |
| Edit scope | **Inspect plus safe edits.** No joint add/delete/reorder/reparent (meshes index joints by position). |
| Preview mesh for a skeleton or clip | **Auto-pick** the first skinned mesh naming that skeleton (via the catalog); bone lines if none. **Override** per asset, stored in `Library/editor_state.json` by guid, never in the asset file. |

## Units

### Engine

1. **`AnimationModule` evaluates several scenes.** Today it ticks the active scene only. Scenes registered as animated (the preview scene of an open Asset Editor view, the thumbnail scene) are evaluated too, and their palettes reach the render thread through the view's own extraction. Views still do not tick by themselves.
2. **Skeleton sockets.** `SkeletonData::Sockets`: `{ name, joint (name), local position/rotation/scale }`, names unique per skeleton. `.wskel` version bump; older files read with no sockets. Read and write live in `native_anim_files` (the one description of the formats). The cooked `Skeleton` asset carries them (`WASSET_VERSION` bump), the `Animator` gets no new behaviour in B2 (socket world transform lookup is C/Lua scope).
3. **Thumbnail render path.** Uses A's Thumbnail profile, `make_preview_scene`, `make_orbit_camera` (framed from bounds) and `RenderView::read_back`.

### Editor

4. **`ThumbnailCache`** (`editor/src/assets/`). `request(guid) -> texture id` returns a placeholder until the thumbnail exists.
   - Key: `guid + hash(cooked bytes of the asset and of its dependencies: skeleton, materials, preview mesh) + renderer version`. Cooked bytes already exist in `Library`.
   - Hit: PNG decoded on a worker, uploaded on the main thread. Miss: rendered.
   - One shared 128x128 transparent `RenderView` (Thumbnail profile) reused for every thumbnail; the preview scene is rebuilt per asset. Budget: 2 renders per frame. Only visible cells request; a cell that scrolls out cancels its pending request.
   - Content: skeleton = bone lines; skinned mesh = posed model; clip = first frame on the auto-picked mesh (bone lines if none).
   - Disk: PNG via `png_writer`; capped at about 512 MB, oldest first; a new entry for a guid deletes the stale ones.
   - Failure: type icon, no retry until the asset changes, logged once.
5. **Asset browser** shows type icons and thumbnails for skeleton, clip and skinned mesh (grid and list), tooltips with stats (joints, duration, vertices) and the model they were extracted from. Double-click opens the Asset Editor. Context menu: *Open in Asset Editor*, *Find references* (exists), *Re-extract from model...* (writes new files from the model's current data under a fresh name; never overwrites; uses the B1 extractor).
6. **`AssetEditorPanel`** hosts one `IAssetEditor` at a time. Opened from the browser or the Inspector. Owns the preview `RenderView` (orbit camera, grid, bone/joint-name/socket overlays, mesh toggle) and the shared chrome: title with `*` when dirty, Save (Ctrl+S), Undo/Redo (Ctrl+Z/Ctrl+Y while focused), external-change Reload prompt, Save/Discard/Cancel when closing or switching while dirty.
7. **`PreviewMeshResolver`**: skeleton guid -> mesh guid. Catalog lookup for the default; override in `Library/editor_state.json`.
8. **`SkeletonEditor`**
   - Joint tree: name, parent, count of meshes and clips using the joint. Selecting highlights it in the preview. F2 / double-click renames; names must be non-empty and unique (refused inline).
   - Sockets: add, delete, rename, pick joint, edit offset; ImGuizmo translate/rotate gizmo on the selected socket in the preview.
   - Preview: bind pose, or a compatible clip from a dropdown (found through the catalog) with play/pause and a time slider.
9. **`MeshEditor`**
   - Info: skeleton (click opens it in the same panel), joint count, vertex and triangle counts, bounds, joint-name compatibility.
   - Material slots: one material picker per slot.
   - Rebind skeleton: allowed only when `clip_compatibility`-style check is Full (every joint name in the mesh's skin exists in the target); otherwise refused listing the missing names.
   - Preview: bind pose or a compatible clip, same overlays.
10. **Joint rename propagation.** Save with a renamed joint shows a dry-run list of the `.wmesh` and `.wanim` files that name the joint (found via the catalog and a scan of the extracted folders, like `reference_finder`), including ones that cannot be read. Confirm rewrites dependents first, then the skeleton **last**, so a failure never leaves a half-renamed set; a dependent that fails to write is reported and left untouched. Cancel changes nothing.

### Writing and reloading

Edits are held in memory (dirty) and written atomically (temp file then rename, as the Animation panel and extractor do). The existing import watcher re-cooks; the panel reloads through the catalog listener. A file changed on disk while open offers Reload, with a warning when dirty. An unreadable or invalid asset shows its reason in the panel. A failed preview (no compatible mesh, view creation failure) falls back to bone lines with a one-line notice.

Undo/redo: reuse the editor's command stack if one fits; otherwise one stack of before/after snapshots of the editable part (names, sockets, slots) per open asset. Decided at planning after reading the editor code.

## Limits

- At most one thumbnail `RenderView` and one Asset Editor preview `RenderView` (A's `MAX_VIEWS` is 16).
- Joint add/delete/reorder/reparent, clip editing, animation graph: out of scope (B3, C).
- Thumbnails for static meshes, materials and textures: not in B2; the `ThumbnailCache` request API is generic enough to add them.
- Socket world-transform queries from Lua and `Animator`: not in B2.

## Docs and verification

Update in the same change: `animation.md` (sockets, multi-scene module), `assets.md` (thumbnail cache, `.wskel` version), `editor.md` (Asset Editor, browser), `views.md` (thumbnail and editor views), the CLAUDE.md area table. Verification is a Release build plus manual checks on Vulkan and D3D11 (no smoke tests added or run; compile-only smoke sources stay compiling). Nothing is committed unless asked; the spec and plan are never committed.

## Review focus (inputs the design implies)

- A skeleton used by no mesh and no clip: preview falls back to bone lines, thumbnail renders bones.
- A clip with Partial/None compatibility on the previewed skeleton: preview shows what binds and a notice, no crash.
- Renaming a joint that a mesh or clip names while some dependents are read-only, missing or open elsewhere.
- Two joints renamed to swap names in one edit (uniqueness checked on the final set, rewrite done in one pass).
- Thumbnail requested for an asset deleted or re-imported mid-render; view removed while a read-back is pending.
- Cold Library or a corrupted `Library/thumbnails/` entry: decode failure re-renders instead of showing garbage.
