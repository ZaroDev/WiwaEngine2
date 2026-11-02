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

time = {}
--- Seconds of the update that is running.
---@return number
function time.delta() end
--- Seconds since the scripts started.
---@return number
function time.total() end

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
---@param definition? ScriptDefinition
---@return ScriptClass
function ScriptClass:extend(definition) end

---@type ScriptClass
---@diagnostic disable-next-line: missing-fields
Script = {}
