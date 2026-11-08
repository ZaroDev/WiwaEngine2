# Scripting (Lua)

Code: `engine/src/scripting`; importer `editor/src/asset_pipeline/importers/script_importer.cpp`; LuaLS tooling `editor/src/asset_pipeline/lua_stubs.*`. User-facing API: `docs/scripting.md` (keep it in sync with `bindings/`). Verified by `--script-smoke`, `--demo-smoke`.

## Shape of a script

A `.lua` file returns a class made with `Script:extend{ properties = {...} }` with optional `on_init`, `on_update(dt)`, `on_destroy`, plus physics, trigger and UI handlers (physics.md, ui.md). `properties` maps onto `FieldDescriptor`s (bool, int, float, vec2/3/4, color, quat, string, enum, asset, asset_list, entity; `min`/`max`, enum `names`, accepted `asset` type). A subclass inherits and may override its parent's properties. A scene-level script is the same class attached in `SceneSettings::Scripts`; it has no `self.entity`.

## Architecture

- Scripts are **value-bag components**: the scene stores property values in `ValueBags`, so serialization, inspector, prefab overrides and play-mode cloning work through reflection. Each script appears as a component named after the script (several per entity); a saved script component names its script by asset guid.
- `ScriptingModule` (priority 0, after the scene -80, UI -10, physics -20) owns one `sol::state`, main thread only. Units: `ScriptRuntime` (loads a Script asset into a class table in its own environment, custom `require`, reads `properties`, registers the value-bag `ComponentInfo`, reverse dependency map, `reload_script`), `ScriptInstances` (per (entity, script) `self` and cached hooks, discovery, lifecycle, failure state), and `bindings/*` (one file per area: math, scene/entity/components, assets and `load_scene`, render/debug draw/camera, log and time, physics, input, ui).
- **Script asset:** `ScriptData` holds the Lua **source** and its path (not bytecode). It is loaded with `sol::load_mode::text` only, since precompiled chunks are unchecked by Lua and a pack could hold a hostile one. The importer compiles with `luaL_loadbuffer` to report syntax errors with file and line (a failed import stays out of the catalog) and statically scans `require "x"` / `require("x")` literals as asset dependencies. A dynamic `require` works in the editor but its target may be missing from a pak (documented limit).
- **Sandbox:** environments expose base (without `dofile`, `loadfile`, file-loading `load`), `math`, `string`, `table`, `utf8` and the engine API. No `io`, no `os`. `require("scripts/util")` resolves a source path under `Assets/` or `engine/` through the catalog, runs the module once in its own environment and caches it; cycles raise a Lua error naming the cycle.
- **`quit()`:** calls `ScriptContext::RequestQuit`. `ScriptingModule` sets it to `Engine::request_quit()`; the editor replaces it (`set_quit_handler`) with a flag that stops play mode in `post_update`, so a script can never close the editor. Empty (a test) makes `quit()` a Lua error.

## Lifecycle

- `update`: (1) discovery finds (entity, script) pairs without an instance, creates `self`, calls `on_init` before that instance's first `on_update`; (2) scene-level scripts in settings order; (3) entity scripts (order between entities is undefined, per entity it is attach order). Entities created by a script during a frame get `on_init` on the next frame's discovery. Physics events/`on_physics_update` and UI events are dispatched by `ScriptInstances` too (physics.md, ui.md).
- `on_destroy` runs on component removal, entity destruction, and scene replace/retire (once, up front, because scene teardown is spread over frames).
- **Scripts run only while the game clock is playing** (not paused). In the editor's edit mode the runtime still loads scripts (so components appear in Add Component and the inspector) but runs no lifecycle; a game runs it as soon as a scene is active.
- Errors: every Lua call is a `sol::protected_function`. A failure logs script, entity, hook and line and marks that instance failed (skipped until reload). Others keep running; play never stops on a script error. A script that fails to load keeps its last good schema (or is unregistered if none).
- `self.entity` is a handle storing an `EntityGuid`, resolved on each access (a destroyed entity gives a clear Lua error). `self.<property>` reads/writes the instance's `ValueBag`, validated (wrong type is a Lua error). Other fields live on the Lua table only: not saved, not inspected, preserved across reloads.
- Lua API reaches any component through `ComponentRegistry` by name (`entity:get("Light")`, `other:get("Enemy").health`), so there are no per-component bindings.

## Hot reload

A re-cooked script reloads it and its dependents (reverse dependency map), swapping methods **in place** in the existing class tables so live instances see the change, re-registering the component and reconciling bags (values whose name and type still match are kept, the rest take defaults). Works during play and in edit mode; a script that failed on first load is reloaded even though the runtime never ran it.

## LuaLS IntelliSense

A hand-written `---@meta` stub `editor/engine/lua/wiwa.lua` documents the static API (a parity check in `--script-smoke` keeps it in step with the bindings). The editor generates `library/lua/generated.lua` from `ComponentRegistry` (components, script properties, typed `Entity:get` overloads) whenever components change, and writes `.luarc.json` (Lua 5.4; `io`, `os`, `debug`, `package` disabled) on project open. A generated `.luarc.json` starts with a `// generated by Wiwa; remove this line to take over the file` marker; a file without it is never overwritten. Output is rewritten only when its text changed, a write failure is a warning, and class names/`get("...")` strings are sanitized/escaped.

## Editor play mode

See editor.md (Play runs on a scene clone; `ScriptingModule::update` is gated on the game clock).

## Animation API

`bind_animation.cpp`: `Animation.animator(entity)` gives a handle (`play`, `crossfade`, `stop`, `layer`, `on`, `off`, `joint_world`, `set`, `get`, `trigger`, `state`, `speed`, `time`, `playing`, `ready`; `set`/`get`/`trigger`/`state` drive the animation graph of `Animator.Graph`: a wrong name or kind is logged once and ignored, writes before the graph has loaded wait); layer handles have `weight`, `crossfade`, `stop`. Handles resolve through the active scene on every use: a destroyed entity or an ended layer reads nil and ignores writes. `ScriptContext` carries the module and `ActiveSceneRef`, so a handle can make its animator instance on demand (scripts run before `AnimationModule::tick`). Clip event handlers live in `AnimHandlerTable`, delivered by `ScriptInstances::dispatch_animation_events` at the start of `update()` (one frame late), owned by the script instance like UI handlers (the owner is `UiHandlerTable::current()`), removed when they raise an error. `AnimationModule` must be registered before `ScriptingModule` (engine.cpp).
