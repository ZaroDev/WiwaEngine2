---@meta
-- The Wiwa scripting API, for the Lua language server (LuaLS). Not a script: the editor does not import it and the game never loads it.
-- Keep it in step with engine/src/scripting (the globals are checked by --script-smoke) and docs/scripting.md.
-- Entity:get / Entity:add are declared in library/lua/generated.lua, which the editor writes from the registered components.

---@alias PropertyType "bool"|"int"|"float"|"vec2"|"vec3"|"vec4"|"color"|"quat"|"string"|"enum"|"asset"|"asset_list"|"entity"

--------------------------------------------------------------------------------------------------------------------------------
-- Math. Angles are degrees.
--------------------------------------------------------------------------------------------------------------------------------

---@class vec2
---@field x number
---@field y number
---@operator add(vec2): vec2
---@operator sub(vec2): vec2
---@operator unm: vec2
---@operator mul(vec2): vec2
---@operator mul(number): vec2
---@operator div(vec2): vec2
---@operator div(number): vec2
---@overload fun(): vec2
---@overload fun(s: number): vec2
---@overload fun(x: number, y: number): vec2
vec2 = {}
---@return number
function vec2:length() end
---@return vec2
function vec2:normalize() end
---@param other vec2
---@return number
function vec2:dot(other) end
---@param other vec2
---@return number
function vec2:distance(other) end
---@param other vec2
---@param t number
---@return vec2
function vec2:lerp(other, t) end

---@class vec3
---@field x number
---@field y number
---@field z number
---@operator add(vec3): vec3
---@operator sub(vec3): vec3
---@operator unm: vec3
---@operator mul(vec3): vec3
---@operator mul(number): vec3
---@operator div(vec3): vec3
---@operator div(number): vec3
---@overload fun(): vec3
---@overload fun(s: number): vec3
---@overload fun(x: number, y: number, z: number): vec3
vec3 = {}
---@return number
function vec3:length() end
---@return vec3
function vec3:normalize() end
---@param other vec3
---@return number
function vec3:dot(other) end
---@param other vec3
---@return number
function vec3:distance(other) end
---@param other vec3
---@param t number
---@return vec3
function vec3:lerp(other, t) end
---@param other vec3
---@return vec3
function vec3:cross(other) end

---@class vec4
---@field x number
---@field y number
---@field z number
---@field w number
---@operator add(vec4): vec4
---@operator sub(vec4): vec4
---@operator unm: vec4
---@operator mul(vec4): vec4
---@operator mul(number): vec4
---@operator div(vec4): vec4
---@operator div(number): vec4
---@overload fun(): vec4
---@overload fun(s: number): vec4
---@overload fun(x: number, y: number, z: number, w: number): vec4
vec4 = {}
---@return number
function vec4:length() end
---@return vec4
function vec4:normalize() end
---@param other vec4
---@return number
function vec4:dot(other) end
---@param other vec4
---@return number
function vec4:distance(other) end
---@param other vec4
---@param t number
---@return vec4
function vec4:lerp(other, t) end

---@class quat
---@field x number
---@field y number
---@field z number
---@field w number
---@operator mul(quat): quat
---@operator mul(vec3): vec3
---@overload fun(): quat
---@overload fun(x: number, y: number, z: number, w: number): quat
quat = {}
---@return quat
function quat:inverse() end
---@return quat
function quat:normalize() end
--- Euler angles in degrees.
---@return vec3
function quat:to_euler() end
--- Degrees, applied in the order x, y, z.
---@param x number
---@param y number
---@param z number
---@return quat
function quat.from_euler(x, y, z) end
---@param axis vec3
---@param degrees number
---@return quat
function quat.from_axis_angle(axis, degrees) end
---@return quat
function quat.identity() end

---@class mat4
---@operator mul(mat4): mat4
---@overload fun(): mat4
mat4 = {}
---@return mat4
function mat4:inverse() end
---@param p vec3
---@return vec3
function mat4:transform_point(p) end
---@param d vec3
---@return vec3
function mat4:transform_direction(d) end
---@return mat4
function mat4.identity() end
---@param v vec3
---@return mat4
function mat4.translate(v) end
---@param v vec3
---@return mat4
function mat4.scale(v) end
---@param q quat
---@return mat4
function mat4.from_quat(q) end

--------------------------------------------------------------------------------------------------------------------------------
-- Entities, components, assets
--------------------------------------------------------------------------------------------------------------------------------

--- A component of an entity: its fields by name (matched exactly, then ignoring case). Reads and writes go to the entity.
---@class Component
---@field [string] any

--- An entity handle. It never dangles: once the entity is destroyed, is_valid() is false.
---@class Entity
---@field guid string
---@field name string
---@field parent Entity?
---@field children Entity[]
local Entity = {}
---@return boolean
function Entity:is_valid() end
---@param parent Entity? nil makes it a root
---@return boolean
function Entity:set_parent(parent) end

--- An asset, for an asset property. See load_asset.
---@class Asset
---@field guid string
local Asset = {}
---@return boolean
function Asset:is_valid() end

--- A scene that is loading. See load_scene.
---@class SceneLoad
local SceneLoad = {}
---@return number fraction from 0 to 1
function SceneLoad:progress() end
---@return boolean
function SceneLoad:done() end
---@return boolean
function SceneLoad:failed() end
---@return string
function SceneLoad:stage() end

---@param name string
---@param parent Entity?
---@return Entity
function create_entity(name, parent) end

---@param entity Entity
function destroy_entity(entity) end

--- By guid, or by the name of the first entity that has it.
---@param name_or_guid string
---@return Entity?
function find_entity(name_or_guid) end

--- The asset a catalog path names ("assets/textures/rock.png").
---@param path string
---@return Asset
function load_asset(path) end

--- Starts loading a scene and returns at once. "replace" (the default) swaps the active scene when it is complete.
---@param path string
---@param mode? "replace"|"additive"
---@return SceneLoad
function load_scene(path, mode) end

--------------------------------------------------------------------------------------------------------------------------------
-- Rendering, time, log
--------------------------------------------------------------------------------------------------------------------------------

debug_draw = {}
---@param a vec3
---@param b vec3
---@param color? vec4
---@param duration? number seconds
function debug_draw.line(a, b, color, duration) end
---@param min vec3
---@param max vec3
---@param color? vec4
---@param duration? number seconds
function debug_draw.box(min, max, color, duration) end
---@param center vec3
---@param radius number
---@param color? vec4
---@param duration? number seconds
function debug_draw.sphere(center, radius, color, duration) end
---@param from vec3
---@param to vec3
---@param color? vec4
---@param duration? number seconds
function debug_draw.arrow(from, to, color, duration) end

camera = {}
--- The first entity with a Camera marked Primary.
---@return Entity?
function camera.primary() end
--- Makes the entity's camera the primary one.
---@param entity Entity
function camera.set_primary(entity) end

---@class DecalOptions
---@field material Asset the material (.wmat) to project: its base color, normal, ORM and emissive
---@field position? vec3 where on the surface (default the origin)
---@field normal? vec3 the surface's normal: the decal projects along its opposite (default +Z)
---@field size? vec3 the box: width, height and the depth the decal reaches through the surface (default 1, 1, 1)
---@field opacity? number 0 to 1 (default 1)
---@field lifetime? number seconds at full opacity between the fades; 0 or none is permanent
---@field fade_in? number seconds
---@field fade_out? number seconds
---@field sort_order? integer the higher draws later and wins overlaps
---@field roll? number degrees about the normal
---@field parent? Entity
---@field name? string

Decal = {}
--- Makes an entity with a Decal that projects a material onto the surface at `position`; with a lifetime it fades and removes itself.
---@param options DecalOptions
---@return Entity
function Decal.spawn(options) end

---@class VfxOptions
---@field effect Asset the particle effect (.wfx), from load_asset
---@field position? vec3 where (default the origin); an offset from the joint when animator and joint are given
---@field rotation? quat
---@field scale? number|vec3
---@field parent? Entity
---@field name? string
---@field looping? boolean true: plays until stopped or destroyed (default false: plays once and removes itself)
---@field time_scale? number
---@field params? table "color" (vec4), "size", "rate", "time_scale"
---@field animator? Entity an entity with an Animator: place the effect at one of its joints
---@field joint? string the joint of the animator

Vfx = {}
--- Plays a particle effect in a new entity that removes itself when the effect is over (an animation event of a footstep or a sword swing: pass animator and joint).
---@param options VfxOptions
---@return Entity
function Vfx.spawn(options) end
--- Starts the ParticleSystem of the entity (one that has finished starts over).
---@param entity Entity
function Vfx.play(entity) end
--- Stops it spawning; the particles in the air live out their lives, or are discarded at once with clear.
---@param entity Entity
---@param clear? boolean
function Vfx.stop(entity, clear) end
--- Plays it from its beginning with none of the last play's particles left.
---@param entity Entity
function Vfx.restart(entity) end
---@param entity Entity
---@return boolean
function Vfx.is_playing(entity) end
--- "color" (vec4 or vec3), "size", "rate" (numbers of zero or more) or "time_scale".
---@param entity Entity
---@param name string
---@param value number|vec3|vec4
function Vfx.set_param(entity, name, value) end

--------------------------------------------------------------------------------------------------------------------------------
-- Physics
--------------------------------------------------------------------------------------------------------------------------------

--- What a ray or a shape cast hit.
---@class PhysicsHit
---@field entity Entity
---@field point vec3
---@field normal vec3 unit, pointing away from the entity that was hit
---@field distance number metres from the origin of the ray (the start of the cast)

--- The contact of a collision event, from the point of view of the script that receives it.
---@class PhysicsContact
---@field point vec3 world space
---@field normal vec3 unit, from the other entity towards this one (the way this one is pushed out)
---@field impulse number estimated normal impulse, kg m/s
---@field relative_velocity vec3 this entity's velocity relative to the other's, before the contact was resolved

--- A shape for physics.shape_cast: { kind = "sphere", radius = r }, { kind = "box", half_extents = vec3 } or { kind = "capsule", radius = r, half_height = h }.
---@alias PhysicsShape table

--- The rigid body of an entity (`entity:body()`). Commands are applied at the start of the next physics step, so what they do shows one step later.
---@class Body
local Body = {}
---@param force vec3 newtons, world space; acts during one step
function Body:add_force(force) end
---@param impulse vec3 newton seconds, world space
function Body:add_impulse(impulse) end
---@param torque vec3
function Body:add_torque(torque) end
---@param velocity vec3 m/s
function Body:set_velocity(velocity) end
---@param angular vec3 rad/s
function Body:set_angular_velocity(angular) end
--- The velocity in the newest physics step; nil when the entity has no body in the world.
---@return vec3?
function Body:get_velocity() end
---@return vec3?
function Body:get_angular_velocity() end
--- Moves a kinematic body to the pose over the next step (it pushes what it meets). Without a rotation the body keeps its own.
---@param position vec3
---@param rotation? quat
function Body:set_kinematic_target(position, rotation) end
--- Wakes a sleeping body.
function Body:wake() end

--- The entity's rigid body.
---@return Body
function Entity:body() end

--- Queries run against the physics step the handler was called for (on_physics_update and the collision handlers), else the newest step.
--- `layer_mask` has bit n set for physics layer n (nil: every layer). Triggers are not hit.
physics = {}
---@param origin vec3
---@param direction vec3
---@param max_distance number
---@param layer_mask? integer
---@return PhysicsHit?
function physics.raycast(origin, direction, max_distance, layer_mask) end
--- Every hit, nearest first.
---@param origin vec3
---@param direction vec3
---@param max_distance number
---@param layer_mask? integer
---@return PhysicsHit[]
function physics.raycast_all(origin, direction, max_distance, layer_mask) end
---@param center vec3
---@param radius number
---@param layer_mask? integer
---@return Entity[]
function physics.overlap_sphere(center, radius, layer_mask) end
---@param center vec3
---@param half_extents vec3
---@param rotation? quat
---@param layer_mask? integer
---@return Entity[]
function physics.overlap_box(center, half_extents, rotation, layer_mask) end
--- Sweeps a shape from `from` to `to` and gives the first hit (distance along the path).
---@param shape PhysicsShape
---@param from vec3
---@param to vec3
---@param layer_mask? integer
---@return PhysicsHit?
function physics.shape_cast(shape, from, to, layer_mask) end
--- Sets the gravity of the world (the scene's setting applies again when the scene restarts).
---@param gravity vec3
function physics.set_gravity(gravity) end

--- Joints: these write the entity's `Joint` component, the physics world gets the change at the start of the next step.
--- A motor of a hinge turns, and one of a slider moves, the entity that owns the joint relative to the one it is connected to.
joint = {}
--- `mode` is "off", "velocity" (rad/s for a hinge, m/s for a slider) or "position" (degrees, metres). `max_force` (N or N m, nil: as it was)
--- limits the motor; 0 is no limit. Raises an error for an entity without a `Joint` and for an unknown mode.
---@param entity Entity
---@param mode "off"|"velocity"|"position"
---@param target number
---@param max_force? number
function joint.set_motor(entity, mode, target, max_force) end
--- Limits of the joint from its rest pose (degrees for a hinge, metres for a slider): `min <= 0 <= max`; both 0 is no limit.
---@param entity Entity
---@param min number
---@param max number
function joint.set_limits(entity, min, max) end

--- Characters (the `CharacterController` component): commands go to the physics world and take effect in the next step; reads are from the newest step.
--- Up is +Y. A call on an entity without a `CharacterController` is an error.
character = {}
--- The horizontal velocity (m/s, world space) the character tries to keep until the next call; `y` is ignored (gravity and jumps are the physics world's).
---@param entity Entity
---@param velocity vec3
function character.move(entity, velocity) end
--- Jumps with an upward speed (m/s) in the next step, only if the character stands on something then.
---@param entity Entity
---@param speed number
function character.jump(entity, speed) end
--- True when it stands on ground it can walk on.
---@param entity Entity
---@return boolean
function character.is_grounded(entity) end
--- `grounded`, `sliding` (on a slope too steep to stand on), `ground` (the entity it stands on, nil for none), `ground normal`, and its real `velocity`; nil while the world has no such character.
---@class CharacterState
---@field grounded boolean
---@field sliding boolean
---@field ground Entity?
---@field normal vec3
---@field velocity vec3
---@param entity Entity
---@return CharacterState?
function character.state(entity) end

--------------------------------------------------------------------------------------------------------------------------------
-- Input
--------------------------------------------------------------------------------------------------------------------------------

--- A control is named "Key.W", "Mouse.Left", "Mouse.DeltaX" or "Pad.A", "Pad.LeftX" (device, dot, name). Gamepads are numbered from 0.
---@alias InputControl string
---@alias InputCursorMode "normal"|"hidden"|"locked"
---@alias InputBindingKind "control"|"composite1d"|"composite2d"|"pair2d"
---@class InputBinding
---@field kind InputBindingKind
---@field controls InputControl[] the controls of the binding, in the order of `parts`
---@field parts string[] "control", "negative"/"positive", "up"/"down"/"left"/"right" or "x"/"y"
---@field scale number
---@field deadzone number
---@field invert boolean
---@class InputConflict
---@field map string
---@field action string
---@field binding integer 1-based

--- A named action of an input map (a .winput asset). It reads the state of the current frame.
---@class InputAction
local InputAction = {}
---@return boolean
function InputAction:down() end
---@return boolean
function InputAction:pressed() end
---@return boolean
function InputAction:released() end
--- The magnitude of a button, the value of a 1D axis (signed).
---@return number
function InputAction:value() end
--- The value of a 2D axis, at most length 1.
---@return vec2
function InputAction:vector() end

--- Queries read false / zero while the game clock does not play. Errors: an unknown control or action is a Lua error that names it.
input = {}
--- `pad` is the gamepad (0-based); without it a Pad control reads the largest value of all connected pads.
---@param control InputControl
---@param pad? integer
---@return boolean
function input.down(control, pad) end
---@param control InputControl
---@param pad? integer
---@return boolean
function input.pressed(control, pad) end
---@param control InputControl
---@param pad? integer
---@return boolean
function input.released(control, pad) end
--- 0 or 1 for a button, the raw value of an axis (sticks -1..1 with up = +1, triggers 0..1).
---@param control InputControl
---@param pad? integer
---@return number
function input.value(control, pad) end
--- Pixels of the window.
---@return vec2
function input.mouse() end
---@return vec2
function input.mouse_delta() end
---@return vec2
function input.scroll() end
---@param pad integer
---@return boolean
function input.pad_connected(pad) end
--- Which device was used last, for button prompts.
---@return "keyboard_mouse"|"gamepad"
function input.last_device() end
---@param mode InputCursorMode
function input.set_cursor(mode) end
---@param map string
---@param name string
---@return InputAction
function input.action(map, name) end
--- A map is read only while it is on the stack; a map that consumes controls hides them from the maps below it.
---@param map string
function input.push_map(map) end
---@param map string
function input.pop_map(map) end
--- The bindings of an action, for showing them to the player.
---@param map string
---@param action string
---@return InputBinding[]
function input.bindings(map, action) end
--- Rebinds one binding (1-based index) of an action, or one part of it ("up" of WASD). The rebind is always applied; the result is true,
--- or false and the bindings that use the control already, so that the script can ask the player what to do.
---@param map string
---@param action string
---@param index integer
---@param control InputControl
---@param part? string
---@return boolean
---@return InputConflict[]?
function input.rebind(map, action, index, control, part) end
---@param map string
---@param action string
---@return boolean
function input.reset(map, action) end
function input.reset_all() end
--- Calls back with the next control the player presses (a button, or a stick or trigger past half). Escape cancels without a call.
---@param callback fun(control: InputControl)
function input.listen(callback) end
--- Whether a listen is waiting (it ends when a control was heard, on Escape, or after cancel_listen).
---@return boolean
function input.listening() end
function input.cancel_listen() end
--- Writes the rebinds so that they are there the next time the game starts.
---@return boolean
function input.save_bindings() end

--------------------------------------------------------------------------------------------------------------------------------
-- UI
--------------------------------------------------------------------------------------------------------------------------------

---@alias UiEvent "click"|"value_changed"|"hover_enter"|"hover_exit"|"focus"
---@alias UiNav "up"|"down"|"left"|"right"|"submit"|"cancel"

--- A widget of a UI panel as one canvas shows it. Reading a widget whose canvas is gone gives nil, and writing it does nothing. The
--- properties are the fields of the widget, written in snake_case: `text`, `value`, `min`, `max`, `step`, `visible`, `enabled`,
--- `opacity`, `color`, `font_size`, `world_anchor`, `world_offset`, and so on (the fields of the panel editor's inspector). Vectors are
--- assigned whole: `w.color = vec4(1, 0, 0, 1)`.
---@class UiWidget
---@field text string
---@field value number
---@field min number
---@field max number
---@field step number
---@field visible boolean
---@field enabled boolean
---@field opacity number
---@field color vec4
---@field font_size number
---@field world_anchor Entity? the entity the widget follows on the screen; nil to stop
---@field world_offset vec3 world units from the entity
---@field [string] any
local UiWidget = {}
--- Calls `handler(self, ...)` when the widget raises the event. `self` is the script instance; `value_changed` also passes the new value.
--- Call it from a hook (on_init, on_update, ...): the handler goes away with the script instance.
---@param event UiEvent
---@param handler fun(self: ScriptClass, value?: number)
function UiWidget:on(event, handler) end
--- Removes this script's handlers for the event, or only `handler`.
---@param event UiEvent
---@param handler? function
function UiWidget:off(event, handler) end
--- Stops the widget following an entity.
function UiWidget:clear_world_anchor() end

--- The panel shown by a UICanvas.
---@class UiPanel
---@field ready boolean the panel is loaded
---@field visible boolean? hides the whole panel
local UiPanel = {}
--- The widget at a path of names from the root, "Menu/Start". nil if there is none.
---@param path string
---@return UiWidget?
function UiPanel:find(path) end

UI = {}
--- The panel shown by the entity's UICanvas, or nil if it has none.
---@param entity Entity
---@return UiPanel?
function UI.panel(entity) end

--- Input made by the script. It reaches the UI only while the UI reads scripted input (tests and tools; the game has no input system yet).
UI.input = {}
--- Pixels of the canvas.
---@param x number
---@param y number
function UI.input.move_to(x, y) end
function UI.input.clear_pointer() end
---@param button integer 0 is the main button
function UI.input.press(button) end
---@param button integer
function UI.input.release(button) end
---@param name UiNav
function UI.input.nav(name) end
---@param amount number
function UI.input.scroll(amount) end

--- What a clip event hands to its handler.
---@class AnimationEvent
---@field name string
---@field clip string the clip that fired it
---@field time number seconds on the clip's own time

--- A clip by name (one of the animator's clips) or an asset from load_asset.
---@alias ClipRef string|Asset

---@class AnimationPlayOptions
---@field loop? boolean default true
---@field speed? number default 1

---@class AnimationLayerOptions
---@field mask? string a joint name: the layer reaches that joint and everything below it
---@field weight? number default 1
---@field additive? boolean adds the change of the clip from its first frame to the pose below; default false
---@field loop? boolean default false

--- A layer above the base animation. Nil fields and ignored writes once the layer has ended.
---@class AnimationLayer
---@field weight number? 0 to 1
local AnimationLayer = {}
--- Fades to another clip (an additive layer swaps its clip at once).
---@param clip ClipRef
---@param seconds? number
---@param options? AnimationPlayOptions
function AnimationLayer:crossfade(clip, seconds, options) end
--- Fades the layer out over some seconds, then removes it.
---@param seconds? number
function AnimationLayer:stop(seconds) end

--- Plays the clips of an entity's Animator.
---@class AnimatorHandle
---@field speed number? the Animator's Speed
---@field time number? seconds of the clip that plays
---@field playing boolean? a clip plays and has not finished
---@field ready boolean? the skeleton has loaded
local AnimatorHandle = {}
--- Plays a clip now. An unknown name is an error that lists the clips.
---@param clip ClipRef
---@param options? AnimationPlayOptions
function AnimatorHandle:play(clip, options) end
--- Fades from what plays to a clip.
---@param clip ClipRef
---@param seconds? number
---@param options? AnimationPlayOptions
function AnimatorHandle:crossfade(clip, seconds, options) end
--- Fades to the rest pose.
---@param seconds? number
function AnimatorHandle:stop(seconds) end
--- A named layer above the base; a layer of the same name is replaced.
---@param name string
---@param clip ClipRef
---@param options? AnimationLayerOptions
---@return AnimationLayer?
function AnimatorHandle:layer(name, clip, options) end
--- Calls the function when a clip fires an event of that name, one frame later. Call it from a script hook; it ends with the script.
---@param event string
---@param handler fun(self: table, event: AnimationEvent)
function AnimatorHandle:on(event, handler) end
---@param event string
function AnimatorHandle:off(event) end
--- The world matrix of a joint as of the last evaluated pose. nil if the animator is not running.
---@param joint string
---@return mat4?
function AnimatorHandle:joint_world(joint) end
--- Sets a parameter of the Animator's animation graph (a number, or a boolean for a bool parameter). An unknown name, a trigger or a value of another kind is logged once and ignored. Safe to call every frame.
---@param name string
---@param value number|boolean
function AnimatorHandle:set(name, value) end
--- The value of a graph parameter (a boolean for a bool or trigger parameter), or nil.
---@param name string
---@return number|boolean|nil
function AnimatorHandle:get(name) end
--- Raises a trigger parameter of the graph for one frame (the transition that waits for it consumes it).
---@param name string
function AnimatorHandle:trigger(name) end
--- The name of the active state of a graph layer (a layer name or a 1-based number; the base layer when omitted), or nil when the Animator plays no graph.
---@param layer? string|integer
---@return string?
function AnimatorHandle:state(layer) end

Animation = {}
--- The animator of an entity that has an Animator, or nil.
---@param entity Entity
---@return AnimatorHandle?
function Animation.animator(entity) end

--- An event that was started with Audio.post. It stays valid to pass to Audio.stop after the event ended.
---@class AudioHandle
local AudioHandle = {}
---@return boolean
function AudioHandle:is_valid() end

--- Audio through the project's backend (Wwise, or Null when none is available). Names are the vendor's own (Wwise event, RTPC, switch and state names).
Audio = {}
--- Starts an event from the entity (it needs an AudioEmitter component, whose Event may be empty) or globally without one. Returns a handle, or nil when the event did not start (an unknown name is logged once).
---@param event string
---@param entity? Entity
---@return AudioHandle?
function Audio.post(event, entity) end
---@param handle AudioHandle
function Audio.stop(handle) end
--- Stops everything the entity's emitter plays.
---@param entity Entity
function Audio.stop_entity(entity) end
function Audio.stop_all() end
--- A game parameter (RTPC): global, or of one entity.
---@param name string
---@param value number
---@param entity? Entity
function Audio.set_rtpc(name, value, entity) end
---@param group string
---@param state string
---@param entity? Entity
function Audio.set_switch(group, state, entity) end
---@param group string
---@param state string
function Audio.set_state(group, state) end
--- Loads a SoundBank asset (a .bnk) in the background.
---@param bank Asset
function Audio.load_bank(bank) end
--- The backend that plays: "Null" or "Wwise".
---@return string
function Audio.backend() end

--- The NavAgent of an entity (Nav.agent): it walks the navmesh to a destination while the game plays.
---@class NavAgentHandle
local NavAgentHandle = {}
--- Starts a walk to the point of the navmesh nearest to the destination. False when there is no navmesh, none near the destination, or no route even part of the way (a route that stops short is walked to its end and the state then becomes "no_path").
---@param destination vec3
---@return boolean
function NavAgentHandle:set_destination(destination) end
function NavAgentHandle:stop() end
--- Puts the agent at the point of the navmesh nearest to the position. False if the navmesh is not near it.
---@param position vec3
---@return boolean
function NavAgentHandle:warp(position) end
---@return "idle"|"moving"|"arrived"|"no_path"
function NavAgentHandle:state() end
--- Straight distance to the destination; 0 when the agent is not moving.
---@return number
function NavAgentHandle:remaining_distance() end
---@return vec3
function NavAgentHandle:velocity() end

---@class NavPath
---@field status "complete"|"partial"|"failed"
---@field length number
---@field points vec3[]

--- Navigation: queries against the navmesh of the scene that plays (baked in the Navigation panel). Without a navmesh the queries answer "failed" / nil / false. Vectors that are not finite numbers are errors.
Nav = {}
--- The agent of an entity; an error when it has no NavAgent component.
---@param entity Entity
---@return NavAgentHandle
function Nav.agent(entity) end
--- The straight path between the navmesh points nearest to the two positions. Partial: the walk ends where it comes closest to the target.
---@param from vec3
---@param to vec3
---@param area_mask? integer  bit set of the areas that may be used (default all)
---@return NavPath
function Nav.find_path(from, to, area_mask) end
--- The point of the navmesh nearest to the point within the extents (default 2 x 4 x 2 m), or nil.
---@param point vec3
---@param extents? vec3
---@return vec3?
function Nav.nearest_point(point, extents) end
--- Whether the line from a to b leaves the navmesh (a wall or an edge) before b, and where.
---@param a vec3
---@param b vec3
---@return boolean hit
---@return vec3? point
function Nav.raycast(a, b) end
--- A random point of the navmesh within the radius of the centre (which must be on it), or nil.
---@param centre vec3
---@param radius number
---@return vec3?
function Nav.random_point(centre, radius) end
--- Whether the scene that plays has its navmesh loaded.
---@return boolean
function Nav.has_navmesh() end

time = {}
--- Seconds of the update that is running.
---@return number
function time.delta() end
--- Seconds since the scripts started.
---@return number
function time.total() end

--- The project's identity (Settings panel, Project): read-only strings and numbers.
---@class ProjectInfo
---@field name string
---@field company string
---@field description string
--- "1.2.3", or "1.2.3-label" when the project has a version label.
---@field version string
---@field version_major integer
---@field version_minor integer
---@field version_patch integer
---@type ProjectInfo
project = {}

--- Asks the host to end the program: a game closes, the editor stops play mode.
function quit() end

--- The renderer's quality settings of the project. `set` merges the keys it is given, applies them at once and saves render_quality.json.
---@class GraphicsSettings
---@field vsync boolean
---@field shadow_distance number
---@field shadow_cascades integer 1 to 4
---@field bloom_mips integer 2 to 6
---@field ao_slices integer 1 to 4
---@field ao_steps integer 4 to 16
---@field fxaa boolean
---@field taa boolean TAA replaces FXAA when both are asked for

--- How the active scene is shown. Applied at once, not saved.
---@class SceneLook
---@field bloom boolean
---@field ao boolean
---@field motion_blur boolean
---@field exposure_compensation number stops

settings = {}
settings.graphics = {}
settings.scene = {}
---@return GraphicsSettings
function settings.graphics.get() end
---@param values table<string, boolean|number> an unknown key or a value of the wrong type is an error
function settings.graphics.set(values) end
---@return SceneLook
function settings.scene.get() end
---@param values table<string, boolean|number> an unknown key or a value of the wrong type is an error
function settings.scene.set(values) end

--- The player's own options of this game, kept in user_prefs.json next to the program.
prefs = {}
---@param key string
---@param default any returned when there is no such pref
---@return boolean|number|string|any
function prefs.get(key, default) end
---@param key string
---@param value boolean|number|string
function prefs.set(key, value) end
--- Writes user_prefs.json; false if the file could not be written.
---@return boolean
function prefs.save() end

---@param title string
---@param message string
function log(title, message) end
---@param title string
---@param message string
function log_warning(title, message) end
---@param title string
---@param message string
function log_error(title, message) end

--- Runs a script module once and returns what it returned. `path` is a catalog path ("assets/scripts/util.lua"); the .lua may be left out.
---@param path string
---@return any
function require(path) end

--------------------------------------------------------------------------------------------------------------------------------
-- Scripts
--------------------------------------------------------------------------------------------------------------------------------

---@class ScriptProperty
---@field type PropertyType
---@field default any?
---@field min number?
---@field max number?
---@field tooltip string?
---@field names string[]? the names of an enum's values
---@field asset string? the asset type of an asset or asset_list ("mesh" and so on)

---@class ScriptDefinition
---@field properties table<string, ScriptProperty>?

--- What a script class is, and what `self` is inside its hooks. Declared properties are fields of self.
---@class ScriptClass
---@field entity Entity
---@field transform Component
---@field name string
---@field [string] any
local ScriptClass = {}
function ScriptClass:on_init() end
---@param dt number seconds
function ScriptClass:on_update(dt) end
function ScriptClass:on_destroy() end
--- Once per completed physics step (a fixed rate, 60 Hz by default), before the collision handlers of the next one. dt is the step.
---@param dt number
function ScriptClass:on_physics_update(dt) end
---@param other Entity
---@param contact PhysicsContact
function ScriptClass:on_collision_enter(other, contact) end
---@param other Entity
---@param contact PhysicsContact
function ScriptClass:on_collision_stay(other, contact) end
---@param other Entity
---@param contact PhysicsContact
function ScriptClass:on_collision_exit(other, contact) end
---@param other Entity
function ScriptClass:on_trigger_enter(other) end
---@param other Entity
function ScriptClass:on_trigger_exit(other) end
--- A joint of this entity went over its break force or torque and is gone. `other` is the entity it was connected to, nil for the world.
---@param other Entity?
function ScriptClass:on_joint_break(other) end
--- This entity's NavAgent reached its destination (heard at the start of the next update).
function ScriptClass:on_nav_arrived() end
--- This entity's NavAgent found no path, or its route stopped short of the destination.
function ScriptClass:on_nav_failed() end
---@param definition? ScriptDefinition
---@return ScriptClass
function ScriptClass:extend(definition) end

---@type ScriptClass
---@diagnostic disable-next-line: missing-fields
Script = {}
