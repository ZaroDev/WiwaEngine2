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
3. the physics steps that completed (see Physics), then scene-level scripts run `on_update`, then entity scripts do (the order between entities is undefined);
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
- **Engine:** `time.delta()`, `time.total()`; `log(title, message)`, `log_warning`, `log_error`; `quit()` asks the host to end the program (a game closes, the editor's Play stops).
- **UI:** `UI.panel(entity)`, widgets by path, `widget:on(event, fn)`, `UI.input` (see "UI" below).

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
- **Input.** The game UI reads the mouse, the keyboard and the gamepads through the input module (see [Input](#input)): the cursor is the
  pointer, and the navigation (arrows, d-pad and left stick, `Enter` / pad `A` to submit, `Escape` / pad `B` to cancel) comes from the
  built-in `UI` action map, so it can be rebound. In the editor's play mode the Viewport gives the game the keyboard and the mouse while it
  has the focus or the pointer (`Escape` or `Shift+F1` hands them back, a click on the image takes them again). `UI.input` (`move_to`,
  `press`, `release`, `nav`, `scroll`, `clear_pointer`) writes to a scripted input source, which only reaches the UI while it is the one in
  use (tests and tools).

## Physics

Entities with a `Collider` (and a `RigidBody` for anything that moves) are simulated by Jolt on its own thread at a fixed rate (60 Hz by
default). Scripts see the world in whole steps, on the main thread, and only while the game clock plays.

Each frame, before `on_update`, every physics step that completed since the last frame is delivered in order: first its collision and
trigger handlers, then `on_physics_update(self, dt)` of every script (`dt` is the fixed step). If the game falls far behind, only the newest
32 steps get `on_physics_update`; the older ones still deliver their enters and exits (never their stays).

| Hook | Called on | Arguments |
| --- | --- | --- |
| `on_physics_update` | every script | `dt` |
| `on_collision_enter` / `on_collision_exit` | scripts on the two entities of a touching pair | `other` (Entity), `contact` |
| `on_collision_stay` | the same, once per step, only if one of the bodies has `ReportContacts` | `other`, `contact` |
| `on_trigger_enter` / `on_trigger_exit` | scripts on the two entities when either collider is a trigger | `other` |

`contact` is described from the receiver's side: `point` (world space), `normal` (unit, from the other entity towards the receiver, the
way it is pushed out), `impulse` (an estimate of the normal impulse, kg m/s) and `relative_velocity` (the receiver's velocity relative to the
other's, before the contact was resolved). `impulse`, `point` and `normal` are not set on exits. Events for an entity that was destroyed
meanwhile are dropped.

**Queries** run against the step being delivered (so inside `on_physics_update` and the collision handlers they see exactly that step), and
against the newest step elsewhere. Triggers and shapes that do not collide are not hit. `layer_mask` has bit n set for physics layer n
(`nil` is every layer).

- `physics.raycast(origin, direction, max_distance, layer_mask)` is `{ entity, point, normal, distance }` or `nil`; `physics.raycast_all(...)` is an array of them, nearest first.
- `physics.overlap_sphere(center, radius, layer_mask)` and `physics.overlap_box(center, half_extents, rotation, layer_mask)` are arrays of entities.
- `physics.shape_cast(shape, from, to, layer_mask)` with `shape` as `{ kind = "sphere", radius = r }`, `{ kind = "box", half_extents = vec3 }` or `{ kind = "capsule", radius = r, half_height = h }`, is the first hit like `raycast`.

**Body control** goes to the physics thread and is applied at the start of the next step, so what you do in step N shows in the
snapshot of step N+1: `entity:body()` is a `Body` with `add_force` (during one step), `add_impulse`, `add_torque`, `set_velocity`,
`set_angular_velocity`, `get_velocity`, `get_angular_velocity` (the newest step; `nil` without a body), `set_kinematic_target(position, rotation)` and `wake()`.
`physics.set_gravity(vec3)` changes the world's gravity. A command for a body that no longer exists is ignored.

```lua
return Script:extend{
  on_collision_enter = function(self, other, contact)
    if contact.impulse > 50 then log("hard landing on " .. other.name) end
  end,
  on_physics_update = function(self, dt)
    local hit = physics.raycast(self.entity:get("Transform").position, vec3(0, -1, 0), 1.2)
    if hit and input_jump then self.entity:body():add_impulse(vec3(0, 300, 0)) end
  end,
}
```

## Input

Keyboard, mouse and gamepad input comes in two layers. **Raw** queries ask about one control; **actions** are named, typed and rebindable
(a `.winput` asset made in the Asset Browser's Create menu, plus the player's rebinds). Both only mean something while the game clock plays:
outside it every query reads `false` / `0`. A control is written `"Key.W"`, `"Mouse.Left"`, `"Mouse.DeltaX"` or `"Pad.A"`, `"Pad.LeftX"`
(device, dot, name); a name that does not exist is a Lua error that says which.

- **Keys:** `A`..`Z`, `0`..`9`, `Space`, `Enter`, `Escape`, `Tab`, `Backspace`, `Left`, `Right`, `Up`, `Down`, `LeftShift`, `LeftCtrl`,
  `LeftAlt`, `F1`..`F12`. **Mouse:** buttons `Left`, `Right`, `Middle`, `X1`, `X2`; axes `DeltaX`, `DeltaY`, `ScrollX`, `ScrollY`.
  **Pad:** buttons `A B X Y LeftBumper RightBumper Back Start Guide LeftThumb RightThumb DPadUp DPadRight DPadDown DPadLeft`; axes `LeftX`
  `LeftY` `RightX` `RightY` (sticks, up is +1) and `LeftTrigger` `RightTrigger` (0 to 1).
- **Raw.** `input.down(c)`, `input.pressed(c)`, `input.released(c)` (a press and release inside one frame gives both), `input.value(c)`,
  `input.mouse()`, `input.mouse_delta()`, `input.scroll()` (`vec2`), `input.pad_connected(pad)`, `input.last_device()` (`"keyboard_mouse"`
  or `"gamepad"`, for button prompts) and `input.set_cursor("normal" | "hidden" | "locked")` for mouse-look. Gamepads are numbered **from 0**;
  without a pad argument a Pad control reads the largest value of all connected pads: `input.value("Pad.LeftX", 1)`.
- **Actions.** `input.action("Gameplay", "Jump")` is a handle with `:down()`, `:pressed()`, `:released()`, `:value()` (a number) and
  `:vector()` (`vec2`, at most length 1). A name that does not exist is an error (`unknown input action 'Gameplay/Jum'`). An action is read
  only while its map is on the stack: `input.push_map("UI")` / `input.pop_map("UI")`. A map marked *consume* hides the controls it binds from
  the maps below it, so a menu does not also make the character jump.
- **Rebinding.** `input.bindings(map, action)` lists the bindings for display (`kind`, `controls`, `parts`, `scale`, `deadzone`, `invert`).
  `input.rebind(map, action, index, control [, part])` changes one binding (**1-based** index, like the list; `part` is `"up"` for the up key
  of WASD) and returns `true`, or `false` and the list of `{ map, action, binding }` that already use the control. The rebind is applied in
  both cases, so the script (or the player) decides what to do about a conflict. `input.reset(map, action)`, `input.reset_all()`,
  `input.save_bindings()` (writes `input_overrides.json`, which is read the next time the game starts).
  `input.listen(function(control) ... end)` calls back once with the next control the player presses (a button, or a stick or trigger past
  half; the mouse moving does not count) and `Escape` cancels without a call; actions read zero while it listens.
  `input.listening()` tells whether it still waits (it is how a script notices the cancel), `input.cancel_listen()` ends it.

```lua
return Script:extend{
  on_update = function(self, dt)
    local move = input.action("Gameplay", "Move"):vector()
    self.entity:get("Transform").position = self.entity:get("Transform").position + vec3(move.x, 0, -move.y) * (5 * dt)
    if input.action("Gameplay", "Jump"):pressed() then self.entity:body():add_impulse(vec3(0, 300, 0)) end
  end,
}
```

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

`assets/scenes/demo.wscene` has scripts to try all of this (the sources are in `assets/scripts/`). Press Play and watch the Console:

- **spinner** (gold knot, blue ring, crate C): turns the entity; change `degrees_per_second` or `axis` in the Inspector while playing.
- **bobber** (neon balls): bobs them using `lib/wave.lua`. Edit `wave.sine` there while playing and every ball changes.
- **light_pulse** (podium spot, neon lights): reads and writes the `Light` component.
- **camera_sway** (camera): drifts the camera sideways.
- **scene_overlay** (a scene-level script, in the scene settings): logs on Play and Stop and draws debug shapes around the podium.
- **demo_ui** (the Hud entity, `assets/ui/demo_hud.wuipanel`): the HUD. The score counts up, a health bar breathes, the button toggles the
  "Demo light" and the slider sets its intensity (click and drag with the mouse over the Viewport in Play mode), and the tag over the
  "Beacon" sphere follows it on the screen. It waits for the panel to be `ready` before it looks its widgets up.
- **input_demo** (the "Input demo" ball, `assets/input/demo.winput`): move it with WASD or the left stick, jump with Space or the pad's A, fire (a flash) with the
  left mouse button or the right trigger. In the editor click the Viewport image first (it takes the keyboard and the mouse; Escape gives them
  back). The panel at the right of the HUD shows the last device, the Move vector and the Jump bindings; "Rebind Jump" waits for a key or a
  button, binds Jump to it and saves it to `input_overrides.json`. The project's default map is chosen in the Input panel (`input.wsettings`).

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
