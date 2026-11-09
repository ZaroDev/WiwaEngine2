-- The camera of the main menu scene: a slow arc round the two heroes on the title screen, a front view of them on the hero select
-- screen. A change between the two blends position, pitch and yaw over about a second. Game.stage holds the numbers.
local Game = require("assets/game/scripts/lib/game.lua")

local MenuCamera = Script:extend{}

local BLEND_TIME = 0.9
local BLEND_SPEED = 5

local function lerp_angle(a, b, k)
  local d = (b - a + 180) % 360 - 180
  return a + d * k
end

function MenuCamera:on_init()
  self.t = 0
  self.pose = nil
  self.key = nil
  self.blend = 0
end

function MenuCamera:target()
  local stage = Game.stage
  local heroes = (Game.state == "heroes") or (Game.state == "options" and Game.options_from == "heroes")
  if heroes then
    return "heroes", stage.camera, stage.pitch, 0
  end
  local c = stage.center
  local a = math.rad(stage.arc * math.sin(self.t * 0.12))
  local pos = vec3(c.x + math.sin(a) * stage.radius, c.y + stage.height, c.z + math.cos(a) * stage.radius)
  local d = vec3(c.x - pos.x, c.y - pos.y, c.z - pos.z)
  local pitch = Game.pitch_of(d.y, math.sqrt(d.x * d.x + d.z * d.z))
  local yaw = Game.yaw_of(vec3(-d.x, 0, -d.z)) + stage.shift -- turned a little to the left: the heroes sit right of the menu
  return "title", pos, pitch, yaw
end

function MenuCamera:on_update(dt)
  self.t = self.t + dt
  local key, pos, pitch, yaw = self:target()
  if self.key ~= nil and key ~= self.key then
    self.blend = BLEND_TIME
  end
  self.key = key

  if self.pose == nil then
    self.pose = { pos = pos, pitch = pitch, yaw = yaw }
  elseif self.blend > 0 then
    self.blend = self.blend - dt
    local k = 1 - math.exp(-dt * BLEND_SPEED)
    self.pose.pos = self.pose.pos:lerp(pos, k)
    self.pose.pitch = lerp_angle(self.pose.pitch, pitch, k)
    self.pose.yaw = lerp_angle(self.pose.yaw, yaw, k)
  else
    self.pose.pos, self.pose.pitch, self.pose.yaw = pos, pitch, yaw
  end

  self.transform.position = self.pose.pos
  self.transform.rotation = quat.from_euler(self.pose.pitch, self.pose.yaw, 0)
end

return MenuCamera
