# Input

Code: `engine/src/input`, Lua `scripting/bindings/bind_input.cpp`, editor `editor/src/input` (`InputEditModel`, smoke) and `editor/src/panels/input_panel.*`, importer `importers/input_map_importer.cpp`. Verified by `--input-smoke` and `--input-panel-smoke`. Lua API: `docs/scripting.md`.

`InputModule` (priority -30) is main-thread only; render and physics threads never touch it. It sits after the window (it listens to it) and before the render module (whose ImGui backend chains onto the same GLFW callbacks), and before UI (-10) and scripts (0). Nothing in it depends on editor code. Out of scope: rumble/haptics (GLFW has none), touch, motion sensors, text input/IME (ImGui owns text entry in the editor).

## Raw layer

- `input_codes.h`: `Key`, `MouseButton`, `GamepadButton`, `GamepadAxis` (values independent of GLFW, with name tables for serialization and Lua) and `InputControl { Device, Code }`.
- `RawInputState` (plain data, one per frame): down bitsets plus pressed/released edge bitsets, cursor position and delta, scroll, up to 4 gamepads (connected, buttons + edges, axes in [-1,1], triggers in [0,1]).
- `IInputBackend::begin_frame()/fill()`. `GlfwInputBackend` chains onto the window's GLFW callbacks and calls the previous callback so ImGui keeps working; it accumulates events so a tap shorter than a frame yields both `pressed` and `released`; polls gamepads via `glfwGetGamepadState` (GLFW's mapping database); focus loss releases everything; the cursor's starting position is not a movement. `ScriptedInputBackend` injects input for smoke checks (`set_backend`). An event-driven backend was chosen over polling because polling misses short taps and cannot inject input; SDL3 was rejected as a second platform dependency.
- `InputState` holds the frame's `RawInputState` and queries (`down/pressed/released/value`, mouse, scroll, `last_device`). **Capture gate:** `set_capture(false)` mutes keyboard and mouse queries but never gamepads. Cursor mode (normal, hidden, locked) is applied through the backend.

## Actions

`ActionSystem` evaluates named, typed actions (`Button`, `Axis1D`, `Axis2D`) from `ActionMap`s on a stack. A binding is a control, a 1D composite (negative/positive) or 2D composite (up/down/left/right, WASD), or a pair (x/y controls, e.g. a stick or mouse delta) with scale, deadzone and invert. Per frame each action gets value (vec2 clamped to unit length for Axis2D), `down`, `pressed`, `released`; among several bindings the largest magnitude wins; edges come from the previous `down` plus raw edges so single-frame taps survive. Queries resolve from the top of the stack down; a `consume` map hides its controls from the maps below. Map and action names are the stable identity (Lua, override file).

## Rebinding and persistence

- `rebind`, `add_binding`, `remove_binding`, `reset_action`, `reset_all`, `conflicts`, `listen_for_control` (Esc cancels; actions do not fire while listening). **A conflict is returned to the caller, never resolved silently.**
- Defaults: a `.winput` asset (JSON `InputMapData`, cooked by `InputMapImporter`, which validates unique names and known controls), named in `input.wsettings` (editor working directory, packed under `INPUT_SETTINGS_GUID`; the packager always packs the map it names).
- Overrides: `input_overrides.json` next to the game (the editor's working directory in the editor). Only actions that differ from the defaults are stored, each with its complete binding list. `save_overrides()` writes; stale entries (action or binding gone) are skipped with a warning.

## UI source

`InputUiSource` is the `IUiInputSource` for the game UI: pointer from the cursor, buttons, scroll, and navigation from a built-in `UI` action map (arrows/WASD/D-pad/left stick, Enter/A submit, Esc/B cancel); a `UI` map in the project's `.winput` replaces it. The game sets it in `GameModule::init`; the editor sets it while a scene plays. `EditorPlayInputSource` (ImGui-fed) remains only for the UI smoke check.

## Editor

- Play routing: the game gets keyboard and mouse only while the Viewport or Game panel (whichever owns the render, `PlayInputRouter`) has the game (click to capture, Esc or Shift+F1 to release); the editor turns the capture gate on only then. Gamepads always reach the game while playing.
- `InputPanel` edits a `.winput` through the ImGui-free `InputEditModel` (maps, actions, bindings, add/remove/rename, undo/redo, a "listen" button for a control) and the default map in `input.wsettings` (edited on the Settings page, Input; the panel forwards to the same `InputSection`).

## Demo

`editor/assets/scenes/demo.wscene` shows it: `demo.winput` (`Gameplay` with Move/Look/Jump/Fire, `UI`), a script moving an entity, HUD readouts of last device and action values, a Rebind Jump button using `input.listen`. `--demo-smoke` drives it with `ScriptedInputBackend`.
