# Scripting (Lua)

Scripts are `.lua` files in `Assets/`. Each one is imported as a `Script` asset, packed into `assets.wpak` when a scene uses it, and
run on the main thread by the `ScriptingModule`.

## A script

A script file returns a class made with `Script:extend`:

```lua
local Mover = Script:extend{
  properties = {
    speed  = { type = "float", default = 5, min = 0, max = 20 },
    target = { type = "entity" },
  },
}

function Mover:on_init() end

function Mover:on_update(dt)
  self.transform.position.x = self.transform.position.x + self.speed * dt
end

function Mover:on_destroy() end

return Mover
```

- Hooks are optional: `on_init`, `on_update(dt)` (seconds), `on_destroy`.
- `self` is the instance. `self.entity`, `self.transform`, `self.name` are the entity's; `self.speed` is the declared property,
  stored on the entity (it is saved in the scene, shown in the inspector and can be overridden by a prefab). Any other field you set
  on `self` lives in the instance only and survives a reload.
- A script can extend another: `local Base = require("assets/scripts/base.lua")` then `Base:extend{...}`. A subclass inherits its
  parent's properties.
- A script file must only *define* things when it loads. It also runs when it is imported, without the engine API, so calling
  `find_entity` and the like at the top level fails the import.

### Properties

`type` is one of `bool`, `int`, `float`, `vec2`, `vec3`, `vec4`, `color`, `quat`, `string`, `enum` (with `names = {...}`), `asset`
and `asset_list` (with `asset = "mesh"` and so on), `entity`. Optional: `default`, `min` and `max`, `tooltip`. Properties are listed in
name order.

## Attaching scripts

- **On an entity:** add the script as a component (it appears in "Add component" under its file name). An entity can have several.
- **On a scene:** a scene-level script has no entity; list it in the scene's `settings.scripts`. It runs while the scene is active.
  Its properties are not stored: they read the declared default until the script assigns its own value.

## Lifecycle

Every frame, after the scene module updated:

1. instances whose entity or component is gone get `on_destroy`;
2. scripts that are new (found by looking at the scene, so entities created by a script during a frame included) get `on_init`;
3. scene-level scripts run `on_update`, then entity scripts do (the order between entities is undefined);
4. world transforms are recomputed.

A script that raises an error is logged (script, entity, hook, line) and that instance is disabled; everything else keeps running.
A hook that runs over 50 million VM instructions is stopped with an error.

When the active scene is replaced, the old scene's scripts get `on_destroy` and the new scene's get `on_init`.

## API

Scripts run in a sandbox: `math`, `string`, `table`, `utf8`, the basic functions without `dofile`, `loadfile` and `load`; no `io`, `os`
or `debug`.

- **Math:** `vec2`, `vec3`, `vec4`, `quat`, `mat4`. `+ - *` and `/`, `length`, `normalize`, `dot`, `distance`, `lerp`, `cross` (vec3).
  `quat.from_euler(x, y, z)` and `quat.from_axis_angle(axis, degrees)` take degrees; `quat * vec3` rotates. `mat4.translate`,
  `mat4.scale`, `mat4.from_quat`, `mat4.identity`, `m:transform_point(v)`, `m:inverse()`.
- **Entities:** `create_entity(name, parent?)`, `destroy_entity(e)`, `find_entity(name_or_guid)` (nil if none). An entity handle has
  `is_valid()`, `name`, `parent`, `children`, `set_parent(e)`, `has(component)`, `get(component)`, `add(component)`,
  `remove(component)`. A handle never dangles: once the entity is destroyed `is_valid()` is false.
- **Components:** `e:get("Light").intensity = 3`. Field names are matched exactly, then ignoring case. A script's component is
  addressed by its file name (`other:get("Enemy").health`); two scripts with the same file name are an error that names both.
  Vectors read from a field write through: `t.position.x = 4` changes the transform.
- **Assets and scenes:** `load_asset(path)` gives the asset a catalog path names (for an asset property); `load_scene(path, mode?)`
  (`"replace"` or `"additive"`) returns a handle with `progress()`, `done()`, `failed()`, `stage()`.
- **Rendering:** `debug_draw.line / box / sphere / arrow(..., color?, duration?)`; `camera.primary()` and
  `camera.set_primary(entity)`.
- **Engine:** `time.delta()`, `time.total()`; `log(title, message)`, `log_warning`, `log_error`.

Input and physics are not bound yet.

## `require` and builds

`require("assets/scripts/util.lua")` takes a catalog path (the `.lua` may be left out) and runs the module once. A cycle is an error
that names the chain. The importer follows `require` calls whose argument is a string literal, so the required scripts are packed
with the script; a computed argument works in the editor but its script may be missing from a build (the importer warns).

## Editor and builds

In the editor scripts are loaded (their components exist) but do not run while you edit. In a game they run as soon as a scene is
active. Packing a scene packs the scripts it uses; a script that nothing references and that is not in the build's "always include"
list is left out.
