# Input module

Date: 2026-11-03. Status: draft for review.

## Goal

Add an `InputModule` that gives the engine keyboard, mouse and gamepad input in two layers: **raw** queries (is this key down, where is the cursor) and **actions** (named, typed, rebindable at runtime). It replaces the stand-ins that exist today (`NullInputSource` in the game, `EditorPlayInputSource` in the editor) and is usable from C++ and Lua.

## Decisions taken in brainstorming

- Backend: **event-driven GLFW backend behind an `IInputBackend` interface**. Polling-only was rejected (misses taps shorter than a frame, no way to inject input in tests). SDL3 was rejected (second platform dependency).
- Action data: **`.winput` asset for defaults + per-user override file** for remaps.
- Scope: core, Lua API, UI integration, `.winput` importer and Input editor panel. Out of scope: gamepad rumble/haptics (GLFW has no API), touch, motion sensors, text input / IME (ImGui keeps owning text entry in the editor).
- Editor Play routing: the game gets keyboard and mouse only while the **Viewport panel is focused or hovered**; otherwise ImGui keeps them. Gamepads always go to the game while playing.
- Every new feature must be shown in `editor/assets/scenes/demo.wscene` (see section 9).

## 1. Module and threading

- `InputModule` (`IModule`) lives in `engine/src/input/`. Registered in `Engine` before `UiModule` and `ScriptingModule`; its `update()` priority is lower than `UiModule`'s (-10) so input is ready first. Concrete value is chosen in the plan against the existing priorities.
- Main thread only, like the scene and scripting. The render and physics threads never touch it.
- The game uses it; nothing in it depends on editor code. No build-config macro decides editor-vs-game behaviour.
- The module itself reads no GLFW state. It consumes an `IInputBackend`, so tests and the editor can swap the source.

## 2. Raw layer

- `input_codes.h`: `Key`, `MouseButton`, `GamepadButton`, `GamepadAxis` enums (values independent of GLFW's, with a table to/from names for serialization and Lua), and `InputControl { Device, Code }` naming one control (for example Keyboard/W, Gamepad/LeftStickX).
- `RawInputState` (plain data, one per frame): key and mouse-button down bitsets plus pressed/released edge bitsets, cursor position and delta (window pixels), scroll, and up to 4 gamepads (connected flag, button bitsets and edges, raw axes in [-1,1], triggers in [0,1]).
- `IInputBackend::begin_frame()` / `fill(RawInputState&)`. Events accumulated between frames set the edges, so a press and release inside one frame yields both `pressed` and `released` (same rule as `UiInputFrame`).
- `GlfwInputBackend`: chains onto the window's existing GLFW callbacks (key, mouse button, cursor, scroll, joystick connect) and calls the previous callback so the ImGui backend keeps working. Gamepads are polled with `glfwGetGamepadState` each frame. Window focus loss releases everything held.
- `ScriptedInputBackend`: injected input for smoke checks and Lua tests (same role as `ScriptedInputSource`).
- Query API on `InputModule`: `is_down(Key|MouseButton|GamepadButton, pad)`, `pressed`, `released`, `axis(GamepadAxis, pad)`, `mouse_position`, `mouse_delta`, `scroll`, `gamepad_connected`, `last_device()`.
- Capture: `set_capture(keyboard_mouse)` gates keyboard/mouse in the module. When capture is off those queries report nothing, while gamepads are unaffected. The editor uses it for Play routing.
- Cursor mode (`normal`, `hidden`, `locked`) is set via the module and applied to the window by the backend, for mouse-look.

## 3. Action layer

- `ActionType`: `Button`, `Axis1D`, `Axis2D`.
- `Binding` is one of:
  - **Control**: one `InputControl` with `scale`, `deadzone`, `invert`.
  - **Composite**: Axis1D (`negative`, `positive` controls) or Axis2D (`up`, `down`, `left`, `right` controls), so WASD becomes a vector.
- `Action { name, type, bindings[] }`; `ActionMap { name, actions[] }`. Names are unique within a map; map and action names are the stable identity (used by Lua and the override file).
- Evaluation, once per frame after the raw state is filled: each action gets `value` (float, vec2 for Axis2D, clamped to unit length), `down`, `pressed`, `released`. Button actions are down when any binding's magnitude passes a press threshold; edges are derived from the previous frame's `down` plus raw edges so single-frame taps are not lost. Among several bindings the largest magnitude wins.
- Map stack: `push_map(name)` / `pop_map(name)` / `set_map_enabled`. A query for an action name resolves from the top of the stack down; a map can be `consume`-flagged so a UI map hides the same control from the Gameplay map beneath it.
- `last_device()` (Keyboard+Mouse vs Gamepad) is derived from which control last produced activity, for UI prompts.

## 4. Rebinding and persistence

- `rebind(map, action, bindingIndex, InputControl)`, `add_binding`, `remove_binding`, `reset_action`, `reset_all`. Conflicts (the control is already used in the same map) are returned to the caller as a list, never resolved silently.
- `listen_for_control(callback)` captures the next pressed control from any device (Esc cancels), for "press a key to bind" screens. While listening, actions do not fire.
- **Defaults**: `.winput` asset, referenced by an `AssetRef` in a new project setting (`input.wsettings`, same mechanism as `physics.wsettings`: working-directory file, packed into `assets.wpak` under a fixed GUID). Source is JSON; the editor importer cooks it to the usual `.wasset`.
- **Overrides**: `input_overrides.json` written next to the game (the editor's working directory in the editor). Only actions whose bindings differ from the defaults are stored, keyed by map and action name, each with its complete binding list (this also covers `add_binding` / `remove_binding`). A binding can also be a **Pair2D** (`x`, `y` controls, for a stick or the mouse delta bound to an Axis2D action). `save_overrides()` writes; the module loads and layers them after the defaults at startup. A stale override (action or binding no longer exists) is skipped with a `WI_LOG` warning.
- Components and settings store `AssetRef`, never `Handle<T>`.

## 5. Lua API

Sandboxed like the rest of `engine/src/scripting` and documented in `docs/scripting.md`. Names are final in the plan; shape:

- Raw: `input.down("W")`, `input.pressed("Space")`, `input.released(...)`, `input.mouse()`, `input.mouse_delta()`, `input.scroll()`, `input.pad_axis(pad, "LeftX")`, `input.pad_down(pad, "A")`, `input.set_cursor("locked")`.
- Actions: `input.action("Gameplay", "Jump")` returns a handle with `:down()`, `:pressed()`, `:released()`, `:value()` / `:vector()`. `input.push_map`, `input.pop_map`.
- Rebinding: `input.rebind(map, action, index, "Key.E")`, `input.reset(map, action)`, `input.listen(callback)`, `input.save_bindings()`, `input.bindings(map, action)` (for display), `input.last_device()`.
- Scripts only run while the game clock is playing, so input queries are only meaningful then; outside Play they return false/zero.

## 6. UI integration

- `InputModule` supplies an `IUiInputSource` implementation (`InputUiSource`): pointer from the cursor mapped to the canvas, `Down/Pressed/Released` from the mouse buttons, scroll, and `NavPressed` from a built-in `UI` action map (arrows/WASD/D-pad/left stick to Up/Down/Left/Right, Enter/A to Submit, Esc/B to Cancel) so the UI nav is itself rebindable.
- The game switches from `NullInputSource` to it. `IUiInputSource` and `UiInputFrame` are unchanged.
- The editor's `EditorPlayInputSource` is kept only for the UI Editor panel's preview; Play mode uses the module with `set_capture` driven by the Viewport panel (focused/hovered captures; click to capture, Esc or Shift+F1 to release). `set_viewport`-style coordinates are given to `InputUiSource` the same way.

## 7. Editor

- `InputMapImporter` in `editor/src/asset_pipeline/importers` (`.winput`), plus an `InputMap` asset type in `engine/src/assets` (CPU-side only, no NVRHI). Importer validates unique names and known control names, and reports errors in the import log.
- `InputEditorPanel` with an ImGui-free `InputEditModel` (maps, actions, bindings, add/remove/rename, undo/redo), in the style of `UiEditModel` / `UiEditorPanel`. Includes a "listen" button to capture a control for a binding and a live column showing each action's current value during Play (or in the panel using the editor's own input via a `ScriptedInputBackend`-free path: ImGui state only for test display).
- The Input project settings (which `.winput` is the default) are edited in the same panel.

## 8. Testing

No test framework; smoke commands with exit codes. A check counts only if seen to fail under a negative control.

- `--input-smoke` (editor): drives `ScriptedInputBackend`. Checks: raw edges including press+release in one frame; mouse delta and scroll; gamepad connect/disconnect and axis; capture gate; Button, Axis1D, Axis2D, composite, deadzone, scale, invert; largest-magnitude binding wins; map stack and consume; rebind, conflict list, listen, override file round trip and stale-override skip; `.winput` import, pack and load with GUID stability; Lua API; `InputUiSource` pointer and nav from gamepad; `--input-smoke` negative controls (for example break an edge rule and see it fail).
- `--input-panel-smoke` (editor): draws the Input panel for 130 frames with ImGui asserts on, edits a binding and checks it was saved.
- `--demo-smoke` is extended to cover the demo scene's input script (below).

## 9. Demo scene

`editor/assets/scenes/demo.wscene` must showcase the feature:

- A `demo.winput` with `Gameplay` (Move Axis2D on WASD/left stick, Look, Jump Button, Fire on mouse/RT) and `UI` maps.
- A script on a demo entity that moves it with `Move`, jumps/fires an impulse (ties into the existing physics demo bodies), and a HUD panel readout (existing demo HUD) showing the last device, the current value of the actions and the key bound to Jump.
- A rebind prompt in the HUD (button "Rebind Jump" calls `input.listen`), and `input.save_bindings()`.
- `--demo-smoke` drives these with `ScriptedInputBackend` and checks the entity moved and the rebinding took effect.

## 10. Docs

- `CLAUDE.md` gets an Input paragraph under Architecture and the new flags in the smoke table.
- `docs/scripting.md` gets the `input` section.

## 11. Open points for the plan

- Exact module priority and the `Engine` registration order.
- Gamepad mapping source: GLFW's built-in SDL mapping database (`glfwGetGamepadState`); a user-supplied `gamecontrollerdb` override is not in scope.
- Names of the Lua functions and the string spelling of controls ("Key.W" vs "W") are fixed in the plan.
