# Game panel and debug draw off in the game

Two changes. Code: `game/src`, `editor/src/panels`, `editor/src/editor_module.cpp`. Docs to update with the change: `rendering.md`, `editor.md`, `input.md`.

## 1. A game starts with debug draw off

`RenderModule::init` enables `DebugDraw` once the renderer runs, so `wiwa_game` drew every `debug_draw.*` call from Lua, the physics debug shapes and the
rest. The game now starts with it **off**; `--debug-draw` on the command line turns it on (the dead `--debug-draw-demo` comment in `game.cpp` is replaced).

- Where: `GameModule::init` (game code, runs after `RenderModule::init`) calls `get_debug_draw().set_enabled(false)` unless `--debug-draw` was given. No engine
  change and no build-configuration macro; the editor is not touched.
- Effect on content: a script's `debug_draw.*` calls draw nothing in the game by default (Orb Rush's dash trail, pickup ring and hit flash are debug draws).
  Gameplay visuals must not depend on debug draw.

## 2. The Game panel

A new editor panel, **Game** (menu Window): the scene seen by the scene's **primary camera**, with no debug draw, no gizmos, no selection outline and no
picking, as Unity's Game view. The game is played in it: it takes the keyboard, mouse and UI pointer while a scene plays.

### One render, one view at a time (decided)

The renderer draws one view per frame into one `ViewportTarget` and its passes keep per-view state (TAA history, exposure, light culling, previous-frame
matrices). A second simultaneous view is a refactor of every pass and was not chosen. Instead the Scene (Viewport) and Game panels **share the one render**:

- A panel that is visible (its `ImGui::Begin` returned true and its content region is not empty) can own the render for the frame. **The Game panel wins**
  when both are visible. Docked as tabs (the usual layout) only the visible tab renders.
- The panel that does not own the render shows a short note instead of an image ("The Game view is showing" / "The Scene view is showing") and does nothing else:
  no camera, no picking, no gizmos, no input capture.
- Ownership is decided by a small shared `ViewOwner` (editor, in `editor/src/panels`): the Game panel stamps `GameFrame = ImGui::GetFrameCount()` when it is
  drawn visible; the Viewport panel treats the Game panel as owner when `GameFrame >= current frame - 1`. This does not depend on the order the panels are drawn
  in (a one-frame lag at the switch is accepted).

### What the owner sets each frame

| | Scene (Viewport, as today) | Game (new) |
| --- | --- | --- |
| viewport size | its content region | its content region |
| camera | the editor fly camera, or primary if "Editor camera" is off | **primary** (`set_camera_override(nullopt)`, UI camera override cleared) |
| debug draw | on (`set_enabled(true)`) | **off** (`set_enabled(false)`: whatever was stored this frame is dropped) |
| selection outline | the selection | **none** (`set_selection(nullopt)`) |
| gizmos, picking | yes | no |
| play input | yes while it owns the render | yes while it owns the render |

The image is the same `get_viewport_texture_id()`. With no primary camera the panel shows "No camera" like the Viewport does. The image fills the content region
(free aspect); no aspect or resolution dropdown.

### Play input moves into a shared helper

`ViewportPanel::update_game_input` and `m_GameReleased` (Escape / Shift+F1 releases, a click takes the input again, `InputModule::set_capture`,
`ui_source().set_viewport(...)`) move into one `PlayInputRouter` used by both panels, each with its own image rectangle. Only the panel that owns the render
calls it, so the game reads input from whichever view is showing it.

### Files

- Create `editor/src/panels/game_panel.h/.cpp` (`GamePanel : Panel("Game", PanelCategory::Window)`), `view_owner.h` (the shared stamp), `play_input_router.h/.cpp`.
- Modify `viewport_panel.h/.cpp` (use the router and the owner; note when the Game panel owns the render; re-enable debug draw when it owns it), `editor_module.cpp`
  (create the `ViewOwner`, add the `GamePanel` before the Viewport panel), `panels.h` if it lists panels, `game_module.cpp` and `game.cpp`.
- Premake globs the folders: re-run `scripts\GenerateProjects2022.bat` (or `2026`) after adding files.

## Not in scope

A second simultaneous view; aspect ratio or resolution presets; switching to the Game tab on Play; showing gizmos or stats overlays in the Game panel;
replacing Orb Rush's debug-draw visuals (the dash trail, pickup ring, hit flash) with scene objects.

## Verification

The user tests by hand; nothing is run. Checklist: in the game, nothing debug-drawn shows (orb rings, dash trail gone); `wiwa_game --debug-draw` shows them.
In the editor: the Game panel shows the primary camera without grid, gizmos, outline or debug shapes while the Viewport still shows them; as tabs, switching
tabs switches the image; side by side, the Game panel renders and the Viewport shows its note; Play, click the Game image, and the game receives the keyboard
and mouse; Escape gives them back; closing the Game panel returns the render to the Viewport.
