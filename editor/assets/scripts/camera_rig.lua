-- The demo camera: drifts sideways, and keys 1 to 7 jump to a station of the hall.
local wave = require("assets/scripts/lib/wave.lua")

-- name, position, pitch and yaw in degrees (plain tables: vec3 is only made while the game runs)
local STATIONS = {
  { "Lighting and post", { 0, 4.5, 17 }, -4, 0 },
  { "Materials and shaders", { 0, 5, -4 }, -6, 0 },
  { "Models and animation", { 0, 2.6, 3.5 }, -5, 180 },
  { "Physics", { 11, 5, 11 }, -22, 0 },
  { "Joints", { -10, 5, 19 }, -16, 0 },
  { "Character controller", { 24, 6, 14 }, -22, 0 },
  { "Scripts, input and UI", { 2, 4, 14 }, -14, 0 },
}

local CameraRig = Script:extend{
  properties = {
    distance = { type = "float", default = 1.5, min = 0, max = 20 },
    speed = { type = "float", default = 0.4, min = 0, max = 5 },
  },
}

function CameraRig:on_init()
  self.base_x = self.transform.position.x
  self.t = 0
end

function CameraRig:go(index)
  local station = STATIONS[index]
  local p = station[2]
  self.base_x = p[1]
  self.transform.position.x = p[1]
  self.transform.position.y = p[2]
  self.transform.position.z = p[3]
  self.transform.rotation = quat.from_euler(station[3], station[4], 0)
  log("Camera", "Station " .. index .. ": " .. station[1])
end

function CameraRig:on_update(dt)
  for index = 1, #STATIONS do
    if input.pressed("Key." .. index) then
      self:go(index)
    end
  end
  self.t = self.t + dt
  self.transform.position.x = self.base_x + self.distance * wave.sine(self.t * self.speed)
end

return CameraRig
