# Script runtime (Lua) — sub-project 1 of 3

Status: design approved in conversation, awaiting written-spec review.

## Context and goal

Gameplay scripting in Lua (sol2 + Lua, already vendored on the `scripting` branch). Scripts are authored as `.lua` files in `Assets/`,
cooked to a `Script` asset, packed into the pak, and run by the engine on the main thread.

The full feature is split in three sub-projects, each with its own spec, plan and implementation:

1. **Script runtime (this spec)**: `Script` asset type, importer, pak, Lua VM, entity and scene script lifecycle, Lua API, script
   properties as value-bag components.
2. **Editor play mode**: scene snapshot/restore, scripts run only while playing.
3. **Hot reload**: re-cook triggers reload of a script and its dependents, state preserved across reloads during play.

This spec builds the entry points sub-projects 2 and 3 need (`start/stop` of the lifecycle, `reload_script`, the dependency map) but not
their editor behaviour.

Requirements gathered:
- Both per-entity scripts and scene-level scripts.
- Properties exposed in the inspector, saved in `.wscene`, overridable by prefabs.
- Scripts packed into the pak; the game project runs without `.lua` files on disk.
- `require` of other Script assets.
- Lua API: math, scene/entity, reflection-based component access, logging, time, asset loading, scene loading, debug draw, camera
  control. Input and physics are not in the engine yet and are out of scope (added as bindings when those systems exist).

## Script shape

A script file returns a class table made with `Script:extend`:

```lua
local Player = Script:extend{
  properties = { speed = { type = "float", default = 5, min = 0, max = 20 }, target = { type = "entity" } },
}
function Player:on_init() end
function Player:on_update(dt) self.transform.position.x = self.transform.position.x + self.speed * dt end
function Player:on_destroy() end
return Player
```

- `properties` is a plain table that C++ reads after loading; it maps onto `FieldDescriptor` (types: bool, int, float, vec2, vec3, vec4,
  color, quat, string, enum, asset, asset_list, entity; `min`/`max`, `names` for enums, `asset` for the accepted asset type).
- A subclass inherits its parent's `properties` and may add or override entries.
- Hooks: `on_init`, `on_update(dt)`, `on_destroy`. All optional.
- Each entity script becomes a component named after the script (shown in "Add Component"); several scripts can be on one entity.
- A scene-level script is the same class, attached in the scene settings instead of on an entity. It has no `self.entity`.

## Architecture

Scripts are **value-bag components**; the Lua side is owned by the runtime, never by the scene.

- The scene stores property values in `ValueBags` (existing). Serialization, inspector, prefab overrides and copying a scene for play
  mode work through the existing reflection layer. `components.h` no longer includes `sol` and `UserScript` is removed.
- `ScriptingModule` owns one `sol::state` (main thread only) and the units below.

### Units

| Unit | Purpose | Depends on |
|------|---------|-----------|
| `ScriptData` + `AssetType::Script` | Cooked asset: Lua source text and its source path. Enum value appended before `Last` (the enum is written to files). Serializer in `wasset_io`. | assets |
| Script importer (`editor/.../importers/script_importer.cpp`, `.lua`) | Compiles with `luaL_loadbuffer` to report syntax errors with file and line (failed import stays out of the catalog); scans for `require "x"` / `require("x")` with literal arguments and emits them as asset dependencies; warns about `require` it cannot resolve statically. Stores **source**, not bytecode. | importer API |
| `ScriptRuntime` | Loads a Script asset into a class table in its own environment; custom `require`; reads `properties` and registers the value-bag `ComponentInfo`; keeps the reverse dependency map; `reload_script(AssetRef)`. | AssetManager, ComponentRegistry |
| `ScriptInstances` | Per (entity, script) `self` table and cached hooks; discovery, `on_init`/`on_update`/`on_destroy`, failure state. | ScriptRuntime, Scene |
| `LuaBindings` (one small file per area) | math, scene/entity/components (reflection), assets and `load_scene`, debug draw and camera, log and time. Each testable alone. | engine modules |
| Scene-level script list | A list of Script assets in `SceneSettings`, saved with the scene. | scene settings |

### Script asset, require, pak

- The packager includes `Script` assets like `Scene`; dependencies emitted by the importer pull in `require`d scripts. Scene import records
  each script a scene uses as a dependency so the scene preload fetches them. A saved script component names its script by **asset
  guid**; the display name comes from the file. (Component-name → asset mapping is detailed in the plan.)
- A dynamic `require(name)` cannot be scanned: it works in the editor but the script may be absent from a pak. Documented limit.
- `require("scripts/util")` resolves as a source path under `Assets/` or `engine/` through the catalog, runs the module once in its own
  environment, caches the result in runtime-owned storage. Cycles are detected and raise a Lua error naming the cycle.
- Environments expose only: base (without `dofile`, `loadfile`, file-loading `load`), `math`, `string`, `table`, `utf8`, and the engine
  API. No `io`, no `os`.

## Lifecycle

- `ScriptingModule::update(dt)` (priority 0 runs after `SceneModule` at -80, so the scene is fully committed):
  1. discovery pass (below), 2. scene-level scripts' `on_update` in settings order, 3. entity scripts' `on_update`. Order between
  entities is undefined by contract; scripts on one entity run in attach order.
- `post_update`: `Scene::update_world_transforms()` so the renderer sees this frame's result. The plan must check who calls it today so
  it is not done twice.
- **Discovery**: each frame, a view over the script components finds (entity, script) pairs without an instance, creates `self`, calls
  `on_init` before that instance's first `on_update`. Covers Additive loads and runtime `create`.
- **Entities created by a script during a frame get `on_init` on the next frame's discovery pass** (decided; avoids re-entrancy).
- **`on_destroy`** runs when the component is removed, the entity destroyed, or the scene replaced/retired. Scene teardown is spread over
  frames by `SceneModule`, so the runtime hooks the scene's retirement and calls `on_destroy` once, up front, then releases the `self`
  tables.
- **Errors**: all Lua calls go through `sol::protected_function`. A failure logs script, entity, hook and line through the engine log
  convention and marks that instance failed (skipped until reload or restart). Other instances keep running. If loading a script fails
  its component keeps its last good schema, or is unregistered if there was none.
- **Threading**: everything Lua runs on the main thread; the renderer thread never touches Lua. Scripts mutate the scene, the existing
  extractor mirrors it.
- **Edit mode (editor)**: the runtime loads scripts (so components appear in "Add Component" and the inspector) but does not run the
  lifecycle. The game project runs the lifecycle as soon as a scene is active. Sub-project 2 adds the start/stop transition.

### `self`

- `self.entity`: handle storing an `EntityGuid`, resolved through the active scene on each access; a destroyed entity gives a clear
  Lua error; `entity:is_valid()` available.
- `self.transform`, `self.name`, `self.parent`, `self.children` via the entity API.
- Properties (`self.speed`) read and write the instance's `ValueBag`; writes are validated with `is_valid_field_value` (wrong type →
  Lua error). Inspector, saved file and script share the value.
- Other fields (`self.timer = 0`) live on the Lua table only: not saved, not in the inspector, and preserved by a reload.

## Lua API (v1)

- **Math**: `vec2`, `vec3`, `quat`, a few `mat4` operations.
- **Scene/entity**: create, destroy, find by name or guid, set parent, children; `entity:get("Light")`, `add`, `has`, `remove`; component
  fields read and written by name through `ComponentRegistry`, so any component, including other scripts' value-bag components
  (`other:get("Enemy").health`), is reachable without per-component binding.
- **Assets**: load an asset by path or ref (through `AssetManager`).
- **Scenes**: `load_scene(path)` → `SceneModule::load_async`, returns a handle with progress. The old scene's `on_destroy` hooks run when
  the replacement is adopted; the new scene's `on_init` hooks run on its first active frame.
- **Debug draw and camera**: through the existing `DebugDraw` and the primary `Camera`.
- **Log and time**: log functions (existing `log`, `log_error`, `log_warning`), delta and total time.

## Hooks for later sub-projects

- Lifecycle `start(scene)` / `stop()` on the runtime, called automatically in the game project and by play mode in the editor.
- `reload_script(AssetRef)`: reloads a script and its dependents from the reverse dependency map, swapping methods **in place** in the
  existing class tables (live instances see the change), re-registering the component and calling `reconcile_bag`. Sub-project 3 adds
  the trigger and editor behaviour.

## Testing

A `--script-test` smoke test, in the style of `scene_smoke_test` / `asset_smoke_test`:
- importer: valid script, syntax error, dependency scan;
- properties: schema reaches the registry, values round-trip through scene save and load;
- lifecycle: `on_init`/`on_update`/`on_destroy` order, next-frame init for script-created entities, teardown on scene replace;
- `require`: loader, cycle error, pak-only run with no `.lua` on disk;
- errors: a throwing script does not stop others;
- bindings: math, entity/component API, `load_scene`, asset load, debug draw, camera.

## Cleanup of the current working tree

- Remove the `UserScript` component and the `script_module.h` include from `components.h`, and its registration in
  `scene_components.cpp`.
- Replace the hello-world script and empty hooks in `script_module.cpp` with the real runtime.
- `engine/vendor/lua` and `engine/vendor/sol2` and the premake entries stay.

## Out of scope

Editor play mode (sub-project 2), hot-reload trigger and state handling (sub-project 3), bytecode precompilation, input and physics
bindings, a Lua debugger, sandboxed file access.
