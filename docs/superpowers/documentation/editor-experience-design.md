# Editor experience overhaul

> Historical design record, written before the area was built. Its status line may be out of date. What exists now is described in editor.md; where the two differ, that doc is right.

Date: 2026-11-07. Status: draft for review. Updates `editor.md` when implemented.

## Goal

Make the editor clearer and faster to use. Success means:

- An asset reference shows which asset it points to (icon, name, path, type, missing state), never a GUID prefix alone.
- Common objects are created in one click, with their components already attached.
- Scene edits can be undone and redone.
- The window layout can be saved, restored and switched, any panel can be maximized, and the UI Editor can fill the editor.

One project, one spec, one plan. Out of scope: autosave and recovery, toasts, multi-selection, camera bookmarks, dropping assets into the Viewport.

## Constraints

- The user does the testing. Do not run any smoke flag. Verify only that the code builds, always with `Release` (`msbuild Wiwa2.sln /p:Configuration=Release /p:Platform=x64`). Re-run `scripts\GenerateProjects2022.bat` after adding files.
- No commits unless the user asks. Never commit specs, plans or PROMPT docs.
- Editor code only, plus small `SceneEditor` hooks. The game never links editor code. No build-config macro decides editor-vs-game behaviour.
- Anything that reads source files (reference scanning) lives in `editor/src/asset_pipeline`.
- Component fields stay described once (`WI_FIELD`). The freeze-row layout is a pure Inspector presentation rule, not a new `FieldType`.
- All UI is main thread. Nothing here touches the render or physics threads.
- The demo-scene rule in `CLAUDE.md` does not apply: this is editor UI, not a runtime feature.
- Conventions: tabs, `#include <editor_common.h>` first, `WI_LOG`, no `near`/`far` identifiers.

## Findings that shape the design

- `InspectorPanel::describe_ref` prints the first 8 characters of the GUID; `draw_asset_field` shows it in a read-only box. `AssetDatabase::path_of(guid)`, `FieldMeta::Asset`, `asset_type_name()` and the asset browser's `get_filetype_icon()` already provide everything a better widget needs. The Inspector only receives `SceneEditor`; it needs the `AssetDatabase`, as the Hierarchy panel already gets it.
- `draw_asset_field` is shared with the Material panel, which benefits automatically.
- `RigidBody` has six bools `LockPositionX/Y/Z`, `LockRotationX/Y/Z`, drawn as six checkboxes.
- The Hierarchy creates only empty entities and prefab instances. `duplicate_entity` and `rename_entity` exist but have no shortcuts.
- The scene has no undo. `AnimationEditModel` is the only precedent (a deque of snapshots, 100 steps).
- `EditorModule::dock_space` owns a full-viewport dockspace; no layout persistence code exists.
- `UiEditorPanel` is already a dockable window (1100x640 first use) with three fixed-ratio child panes.

## Units

### 1. UndoStack (`editor/src/scene/undo_stack.*`)

- Stores snapshots of the scene document produced by `scene_json` (the same path as save), each with a label.
- `SceneEditor` gains `begin_edit(label)`, `commit()` and `cancel_edit()`. Every mutating call records exactly one step: `set_field`, `add_component`, `remove_component`, `create_entity`, `delete_entity`, `duplicate_entity`, `rename_entity`, `reparent`, `add_prefab`, `make_prefab`, `unpack`, `revert_instance`, `revert_field`, paste.
- A drag or a typed edit is one step: the Inspector begins the edit on first change and commits on `IsItemDeactivatedAfterEdit`. Consecutive edits of the same field coalesce.
- Capped at 100 steps and a memory budget (64 MB); the oldest steps drop first.
- `undo()` and `redo()` restore the snapshot, keep the selection by entity guid and put back the dirty flag (undoing to the saved snapshot is clean).
- The stack is cleared when a scene is opened or created. It is disabled while playing (Stop already discards the play clone) and while a scene is opening or a prefab expansion is pending.
- Prefab nodes generate asynchronously; the restore goes through the normal load path so generated nodes reappear in `update()`.
- Risk: snapshots re-serialize the whole scene, fine at demo scale. If a scene is large enough to hitch, the budget evicts sooner; the design does not add per-operation commands.

### 2. ShortcutRegistry (`editor/src/editor_ui/shortcuts.*`)

One table: action id, label, key chord, handler, enabled predicate. Menus show the chord, F1 lists them all, the command palette runs them. Handlers are ignored while `ImGui::GetIO().WantTextInput`. Defaults:

| Chord | Action |
| --- | --- |
| Ctrl+Z / Ctrl+Y (Ctrl+Shift+Z) | Undo / Redo |
| Ctrl+S | Save (exists) |
| Ctrl+D | Duplicate |
| F2 | Rename |
| Ctrl+C / Ctrl+X / Ctrl+V | Copy / Cut / Paste entity |
| Delete | Delete (exists) |
| F | Focus selection |
| Shift+Space | Maximize hovered panel |
| F11 | Maximize the UI Editor |
| Ctrl+P | Command palette |
| F1 | Shortcut cheatsheet |

### 3. Inspector and asset refs (`editor/src/panels/inspector_panel.*`, `asset_field.*`)

- `draw_asset_field` becomes an asset slot: type icon, file name, expected type as a hint when empty, a tooltip with the full source path, the GUID, the sub-asset id and the type, and a red "missing" state (name and short GUID) when the catalog does not know the GUID or the type differs from the field's `Meta.Asset`.
- A drop of the wrong type is rejected; while a payload is dragged the slot highlights when it would accept it and dims when not. The payload check uses `AssetManager::find_asset` and the asset's type.
- Right-click menu: Open, Show in browser (the existing locate), Find references, Copy GUID, Clear. Texture and material slots show a small thumbnail when one is available.
- `AssetList` shows the same slot per element. `Entity` fields show the target entity's name (and a dim "(missing)" when the guid resolves to nothing).
- The signature stays compatible for the Material panel; extra context (expected type, database) is passed through a small `AssetFieldContext` struct with defaults.
- Freeze row: while drawing a component, a run of three consecutive `Bool` fields named `<Prefix>X`, `<Prefix>Y`, `<Prefix>Z` is drawn on one line as `Prefix  [x] X  [x] Y  [x] Z`, like a vector (colored X/Y/Z labels). `LockPositionX..Z` and `LockRotationX..Z` become two rows. Each bool is still set with `set_field`, so overrides, revert markers, serialization and Lua are unchanged. Detection is by name pattern, not by component.
- Component headers get a remove button and a reset-to-defaults item (the context menu stays). The Add component popup gets a search box. The header shows the entity name and an active toggle where the entity has an enabled flag. Component copy/paste moves a component's field values to another entity. Per-field reset to default through right-click. A lock button pins the Inspector to the current entity until unlocked.

### 4. EntityTemplates (`editor/src/scene/entity_templates.*`)

A table of premade objects: name, category, an ordered list of components, field overrides and an optional child list. Created through `SceneEditor` (one undo step each), named uniquely, placed at the origin. Initial set (components that exist in the engine only; unavailable ones are left out when the engine lacks the component):

- Empty
- 3D: Cube, Sphere, Plane, Capsule (mesh and material only; no Collider)
- Light: Directional, Point, Spot
- Camera
- Physics: Rigidbody Box, Rigidbody Sphere (mesh, Collider, RigidBody)
- UI: UI Panel
- Animation: Animator

Mesh primitives use the engine's built-in meshes where they exist; otherwise the entry is omitted rather than invented. The plan must confirm which built-ins exist before listing them.

### 5. Hierarchy (`editor/src/panels/hierarchy_panel.*`)

- A Create menu (header button, background context menu, entity context menu) built from `EntityTemplates`; "Create child" uses the same list.
- Search box; filter by name, with an option to filter by component name.
- Each row shows icons for its main components (mesh, light, camera, collider, rigid body, animator, script) and a visibility toggle.
- Shortcuts: Ctrl+D, F2 (inline rename), Ctrl+C/X/V, F and double-click to focus.
- Dropping an asset on the tree: a Mesh creates an entity with the mesh and a default material; a Prefab instances it (exists); a Material on a row assigns it to that entity's mesh renderer.
- Copy/cut/paste uses an in-memory clipboard of entity JSON (the same format as duplicate, so prefab instances and children survive); paste works across scenes in one editor session. Paste under the selection, or at the top level when nothing is selected.

### 6. Viewport (`editor/src/panels/viewport_panel.*`)

- F frames the selection's bounds with the fly camera. Dropping assets into the Viewport is out of scope.

### 7. LayoutManager and MaximizeState (`editor/src/editor_ui/`)

- Window menu gets a Layouts submenu: built-ins, user layouts, Save current layout..., Reset.
- Built-ins are built with `DockBuilder` code for Default, Level Design, Animation, UI Editing and Debug, over the panels that exist. A built-in that names a panel that does not exist is skipped at that node.
- User layouts are ImGui ini text saved with `SaveIniSettingsToMemory` into `layouts/<name>.ini` beside the editor; loaded with `LoadIniSettingsFromMemory`. The last layout name is stored with the other editor settings and applied at start.
- Maximize: `Panel` gets `m_Maximized`. Shift+Space (or "Maximize" in the tab context menu and the panel's title bar button) toggles the panel under the cursor. While maximized the panel is drawn over the dockspace area and the others are not drawn; the previous layout is the in-memory ini taken at the moment of maximizing and restored exactly on toggle off. Esc also restores. Play-mode toolbar and the menu bar stay visible.
- Because the Game and Viewport panels share the renderer view (`ViewOwner`), maximizing either must keep that rule: the maximized one owns the view.

### 8. UI Editor (`editor/src/ui/ui_editor_panel.*`)

- Maximize button in the header and F11, through MaximizeState.
- Draggable splitters between the tree, canvas and inspector replace the fixed ratios; widths are remembered for the session.
- Canvas zoom with the mouse wheel, pan with middle mouse, Fit to window, and a zoom percent readout.
- The tree gets a search box; Save and Revert sit in the header with undo/redo (the panel already has its own edit-model undo, which stays separate from the scene undo).

### 9. Command palette (`editor/src/editor_ui/command_palette.*`)

Ctrl+P opens a modal search over: shortcut-registry actions, panels (toggle), layouts, entities of the active scene, and assets from the `AssetManager` catalog (selecting an asset locates it in the browser; double-action opens scenes and UI panels). Ranking is a simple fuzzy subsequence score. No new scan: the catalog and the scene already exist.

### 10. Find references (`editor/src/asset_pipeline/reference_finder.*`)

- A background job (on the existing job system) scans `.wscene`, `.wprefab`, `.wmat`, `.wuipanel` and `.winput` sources for the asset's GUID string (and the cooked sub-asset guid is not needed: references are stored as guid plus local id in JSON).
- Results (file, count) show in a popup with a progress bar; clicking a result locates it, double-clicking opens it. Reachable from the Asset Browser context menu and the asset slot menu.
- Reads only; never writes. The engine and game do not include it.

### 11. Edit menu, History and cheatsheet

- A new Edit menu: Undo and Redo (with the step label), Duplicate, Copy, Cut, Paste, Rename, Delete.
- A History panel lists the undo steps; clicking one jumps to it.
- F1 opens a cheatsheet generated from `ShortcutRegistry`.
- Small polish: a status bar (scene name and dirty mark, selection, FPS), recent scenes in the File menu, the first-use window positions replaced by a default layout, a console level filter.

## Build order

1. UndoStack, ShortcutRegistry, Edit menu.
2. Asset slot and Inspector (freeze row, component header work).
3. EntityTemplates and Hierarchy (Create, search, icons, clipboard, drops).
4. Viewport focus (F).
5. LayoutManager and MaximizeState.
6. UI Editor changes.
7. Command palette, Find references, status bar and polish.
8. Update `editor.md`; add a short `editor` section to `CLAUDE.md` only if a new rule appears.

A Release build after each step.

## Risks

- Undo snapshots and async prefab generation: restores go through the load path; the plan must read `scene.md` first.
- Maximize and `ViewOwner`: the maximized panel must own the render view.
- Built-in layouts depend on panel names; keep them in one place.
- This is a large change (roughly 15 new or changed files); order and per-step builds limit the blast radius.
