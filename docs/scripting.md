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
- **UI:** `UI.panel(entity)`, widgets by path, `widget:on(event, fn)`, `UI.input` (see "UI" below).

Physics and keyboard input are not bound yet (the UI has its own input, below).

## UI

A `UICanvas` component on an entity shows a UI panel (a `.wuipanel` asset made in the UI editor). Scripts find the panel through the
entity, then widgets by the path of their names:

```lua
local panel = UI.panel(find_entity("Hud"))          -- nil if the entity has no UICanvas
local score = panel:find("Stats/Score")             -- nil if there is no such widget
score.text = "Score " .. 10
panel:find("Stats/Health").value = 0.5

local start = panel:find("Menu/Start")
start:on("click", function(self) log("UI", "start") end)
panel:find("Menu/Volume"):on("value_changed", function(self, value) self.volume = value end)
```

- **Widgets.** Each canvas has its own copy of the panel, so changing a widget never changes the asset or another canvas. Properties are the
  fields of the widget in snake_case (`text`, `value`, `min`, `max`, `step`, `visible`, `enabled`, `opacity`, `color`, `font_size`,
  `world_anchor`, `world_offset`, ... all the fields the editor's inspector shows). A name that is not a property is an error; `name` is
  read only. Assign vectors whole (`w.color = vec4(1, 0, 0, 1)`). A handle to a widget whose canvas is gone reads `nil` and ignores
  writes. `panel.visible = false` hides the whole panel; `panel.ready` tells whether the asset has loaded.
- **Events.** `widget:on(event, fn)` with `click`, `value_changed`, `hover_enter`, `hover_exit` and `focus`; `fn(self, value)` gets the
  script instance first (and the new value for `value_changed`). Register from a hook (`on_init`, ...): handlers belong to the instance
  and go away with it (destroy, stop, reload that restarts it). `widget:off(event)` removes this script's handlers for the event, or
  `widget:off(event, fn)` one of them. A handler that raises an error is logged and removed; the others and your hooks carry on.
- **Order in a frame.** The UI looks at the input and raises its events *before* the scripts update, so a click in a frame runs its
  handlers before that frame's `on_update`. What scripts change is laid out and drawn in the same frame, after they ran.
- **World anchors.** `w.world_anchor = entity` makes the widget follow the entity on the screen (the widget's pivot sits on its projected
  position, `w.world_offset` is in world units); `w.world_anchor = nil` or `w:clear_world_anchor()` puts it back. Any widget can be
  anchored, containers included: their children come with them. It is a billboard: drawn over the scene, never hidden by geometry.
- **Input.** There is no input system yet. In the editor's play mode the Game viewport's mouse works on the UI. `UI.input` (`move_to`,
  `press`, `release`, `nav`, `scroll`, `clear_pointer`) writes to a scripted input source, which only reaches the UI while it is the one in
  use (tests and tools). When an input system exists it only has to implement `IUiInputSource`.

## `require` and builds

`require("assets/scripts/util.lua")` takes a catalog path (the `.lua` may be left out) and runs the module once. A cycle is an error
that names the chain. The importer follows `require` calls whose argument is a string literal, so the required scripts are packed
with the script; a computed argument works in the editor but its script may be missing from a build (the importer warns).

## Hot reload

Save a script (or a file it `require`s) while the editor is open and the import picks it up; the running scripts switch to the new code
without restarting the scene:

- The new code replaces the old in place, so instances keep `self` and every field set on it (`self.base_y` and the like), and the
  scripts that `require` the changed file are reloaded with it.
- Properties added, removed or retyped show up in the Inspector at once; values that still fit are kept, the rest take the default.
- A script that an error had disabled starts again (a new `self`, `on_init`) with the fixed code.
- A syntax error or bad properties are logged as "Reload failed, the previous version stays" and nothing changes.
- It works in edit mode too (the Inspector follows your edits), not only while playing.
- Limit: state kept in a module that a method writes to (`Base.count = Base.count + 1`) belongs to the version that wrote it; the reload
  copies the module's contents into the old table once, so such a counter restarts from what the file says.

## Editor support (completion and hints in VS Code)

Install the "Lua" extension (sumneko.lua) and open the **`editor/` folder** (the project folder) in VS Code. You get completion, hover
help and warnings for the whole API: `self.transform.position.`, `vec3.`, `debug_draw.`, `e:get("Light").` (the component names and
their fields) and `other:get("Enemy").health` (the properties of your scripts). `os`, `io` and `debug` are flagged, because the
sandbox does not have them.

The editor sets this up itself each time it starts, so there is nothing to configure:

- `.luarc.json` (next to `assets/`) points the extension at the two stub folders. Its first line is a marker: delete that line to
  take the file over, and the editor stops rewriting it.
- `engine/lua/wiwa.lua` is the stub of the fixed API (math, entities, `debug_draw`, ...). It is written by hand; when you add a
  binding to the sandbox, declare it there too. `--script-smoke` fails if the two disagree.
- `library/lua/generated.lua` is written by the editor from the registered components and scripts, and again when they change
  (import, reload). It is not committed. A script shows up in it once the editor has loaded it, which happens at start for every
  script in the project.

## The demo scene

`assets/scenes/demo.wscene` has scripts to try all of this (the sources are in `assets/scripts/`; `scripts/gen_demo_scene.py` puts them
on the scene). Press Play and watch the Console:

- **spinner** (gold knot, blue ring, crate C): turns the entity; change `degrees_per_second` or `axis` in the Inspector while playing.
- **bobber** (neon balls): bobs them using `lib/wave.lua`. Edit `wave.sine` there while playing and every ball changes.
- **light_pulse** (podium spot, neon lights): reads and writes the `Light` component.
- **camera_sway** (camera): drifts the camera sideways.
- **scene_overlay** (a scene-level script, in the scene settings): logs on Play and Stop and draws debug shapes around the podium.

Try: Pause, then Step; edit a script into a syntax error and save (the old version keeps running, the Console says why); fix it; add a
property to `spinner.lua` and see it in the Inspector without stopping.

## Editor and builds

In the editor scripts are loaded (their components exist) but do not run while you edit. In a game they run as soon as a scene is
active. Packing a scene packs the scripts it uses; a script that nothing references and that is not in the build's "always include"
list is left out.

## Limits

- A hook that runs over 50 million VM instructions is stopped, and `pcall` does not swallow that error. The limit counts Lua
  instructions only: one long call into a C library function (a pathological `string.find` pattern, `string.rep` of a huge size)
  is not interrupted, and memory is not capped. Scripts are trusted code written by the project, not a sandbox against hostile ones.
- In `on_destroy` of a script whose entity was destroyed, `self.entity:is_valid()` is false and `self.transform`, `self.name` and the
  properties are not available (the entity is gone); fields you set on `self` still are.
- Scripts cannot add or remove `Id`, `Name`, `Transform` or `PrefabInstance`.
- Numbers assigned to fields must be finite; an `int` property must be in the 32-bit range.
- Script source is text only: precompiled Lua chunks are rejected.
