# Editor

Code: `editor/src` (`editor_module.cpp` wires the panels, menus and smoke flags). The editor is a separate project over the engine lib; the game never links it.

## Panels (`editor/src/panels`)

Hierarchy, Inspector, World, Physics, Input, Viewport, Game, Performance, Console, Material, Asset Browser, UI Editor. Build Settings is a popup, not a panel. Panels draw from their own `on_draw()` with `ImGui::` and have a `PanelCategory` (Window / Info / Renderer menus in `editor_module.cpp`).

- **Hierarchy:** entity tree, create/delete, reparent by drag, context menus, a prefab picker; scene operations are in the File menu. Drop a Prefab asset to add an instance.
- **Inspector:** one widget per `FieldType` from the reflection descriptors; override markers and revert on prefab children; a "locate" button for asset slots selects the asset in the browser.
- **World** (one searchable panel; the old per-effect panels are gone): sections Environment, Lighting, Exposure, Tonemap, Bloom, Ambient occlusion, Temporal AA, Motion blur, Scripts edit the active scene's `SceneSettings` (marks the scene dirty, saved with Save); the Quality section edits `RenderQuality` (global, saved to `render_quality.json`, not with the scene). A search that matches nothing shows "No settings match". Add new effects as sections here.
- **Asset Browser:** create menu with naming-first popup and templates (`asset_templates.*`: name validation rejects path separators, trailing dots, reserved names such as `CON`), drop targets (an entity dragged in becomes a prefab), "Always include in build" context item.
- **Game:** the scene through its primary camera with no debug draw, gizmos, outline or picking (Unity's Game view); the game is played in it. It shares the renderer's one view with the Viewport (the renderer draws one view per frame and its passes keep per-view state): `ViewOwner` records the frame the Game panel was drawn visible, and the Viewport gives way (a note, no camera, selection or input) while that stamp is this frame's or the last one's. Docked as tabs only the visible tab renders; side by side the Game panel wins. While it owns the render it sets: primary camera (`set_camera_override(nullopt)`, UI override cleared), `DebugDraw::set_enabled(false)`, no selection. `PlayInputRouter` (shared by both panels) routes the keyboard and mouse to whichever one shows the render. A second simultaneous view was not built.
- **Viewport:** shows the scene through the ImGui texture registry; editor fly camera (right mouse + WASD); click picking (GPU ID buffer; a click on a gizmo handle wins); selection outline; Play/Pause/Step/Stop toolbar in the secondary bar; a Gizmos menu.
- **Material:** materials.md. **Physics:** physics.md. **Input:** input.md. **UI Editor:** ui.md.
- **Build / Export:** the Export button in the main bar runs `pack_async` to `../game/` behind a blocking progress popup (disabled while a pack runs or during play).

## Gizmos (`editor/src/gizmo`)

Transform gizmo (translate/rotate/scale, W/E/R, world/local, Ctrl snapping, screen-constant size, axis and plane handles, hover highlight) writes the ECS transform directly. Passive gizmos: light shapes (directional arrow, point range sphere, spot cone), camera frustum, selection bounds, grid, axes, outline settings. Toggles persist in `gizmo_settings.json`. Hit-testing, rotation angle, snapping and ray/plane/axis closest-point are pure functions (no GPU, no ImGui) in `engine/src/math/gizmo_math.*`. Everything draws through `DebugDraw`.

## Play mode (`editor/src/play`)

`PlayMode` is a state machine `Edit / Playing / Paused` over an `IPlayHost` (so it is testable with a fake host); `EditorPlayHost` in `editor_module.cpp` is the real one.

- **Play** (only from Edit, only when `can_play()`: no scene opening, no prefab expansion pending): remember the edit scene and document (path, dirty flag, selection guid), `clone_scene`, show the clone as the active scene with `SceneEditor::set_playing(true)`, start the game clock and the scripts.
- **Pause/Resume** toggle the game clock. **Step** (only while paused) un-pauses for exactly one `update()` and re-pauses, running one frame of scripts with that frame's real `dt`.
- **Stop:** `stop_scripts()` first (so `on_destroy` runs while the play scene is still active), then stop the clock, then show the original edit scene and restore the document and selection; the clone is released. Closing the editor while playing stops first.
- If the active scene is replaced during play (a script's `load_scene`), the new scene becomes the play scene and scripts restart on it; Stop still returns to the original edit scene.
- Panels stay live while playing and can inspect/change the clone (changes vanish on Stop). Save/open/new are refused while playing (`set_playing`). Script errors never stop play.
- Physics worlds and the UI/input routing follow the same Play state (physics.md, input.md).

## Performance stats (`engine/src/core/perf_stats.*`, panel `performance_panel.*`)

`Engine::get_perf()` is a main-thread-only `PerfStats`: one `Series` (ring of 600 samples with current, average, max) per metric: main frame ms, per-module ms (`ModuleManager::run_phase` times every module call), render frame / render CPU (drained from the render snapshot by `RenderModule::render_update`), per-pass GPU ms, physics step ms and steps per frame (`PhysicsModule::pump_locked`), process RAM (`GetProcessMemoryInfo`) and VRAM used/budget (DXGI `QueryVideoMemoryInfo` on the adapter matched by the name the backend reports; works for Vulkan, D3D11, D3D12), both sampled four times a second in `system_memory.cpp`. Other threads publish plain data that main drains once per frame. The timers always run (no build-config macro); only the drawing is editor code, and the game can read the same stats.

The Performance panel (replaced the old Renderer panel) shows a header (FPS, API, adapter, shader status, GPU errors), a per-thread table with sparklines (Main expands to per-module cost, Renderer to per-pass GPU), a ~10 s frame-time graph with 16.6/33.3 ms lines, hover readout and pause, RAM and VRAM graphs (VRAM amber above 85% of budget), and scene/cache/upload/physics counters. Graphs use `ImDrawList` polylines (ImPlot is not vendored). Missing data shows "unavailable", never a fake zero. Vsync lives in the World panel's Quality section. Optick is not integrated (only `vendor/Optick/Optick.exe` exists).

## Smoke flags

Each panel or system with a headless check runs through `editor_module.cpp`/its own `*_smoke_test.cpp`; the full table is in CLAUDE.md. A smoke check is trusted only after it was seen to fail under a negative control (break a line, confirm failure, restore). After editing a shader, run `wiwa_editor.exe --render-smoke 100 --api=d3d11` before `--render-test`.
