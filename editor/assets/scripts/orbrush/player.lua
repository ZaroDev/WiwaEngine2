-- Orb Rush: the ball. Moves with the Move action, dashes with Dash. The velocity is set rather than pushed with forces, so the feel does not
-- depend on the body's mass. Only while the manager says the round is on.

local config = require("assets/scripts/orbrush/lib/config.lua")
local session = require("assets/scripts/orbrush/lib/session.lua")

local Player = Script:extend{
  properties = {
    speed = { type = "float", default = 8, min = 1, max = 20 },
    acceleration = { type = "float", default = 10, min = 1, max = 40 },
    dash_speed = { type = "float", default = 16, min = 4, max = 40 },
  },
}

function Player:on_init()
  self.dash_left = 0
  self.cooldown = 0
  self.dash_dir = vec3(0, 0, -1)
end

function Player:on_physics_update(dt)
  local manager = session.manager
  if manager == nil or manager.state ~= "playing" or not manager.pushed then
    return
  end
  local body = self.entity:body()
  local velocity = body:get_velocity()
  if velocity == nil then
    return
  end

  local move = input.action("Gameplay", "Move"):vector()
  local wish = vec3(move.x, 0, -move.y)
  local target = wish * self.speed

  -- a ray down tells whether the ball is on the floor: the dash starts there only
  local from = self.transform.position
  local grounded = physics.raycast(from, vec3(0, -1, 0), 0.8) ~= nil

  self.cooldown = math.max(0, self.cooldown - dt)
  if grounded and self.cooldown <= 0 and input.action("Gameplay", "Dash"):pressed() then
    local flat = vec3(velocity.x, 0, velocity.z)
    if wish:length() > 0.1 then
      self.dash_dir = wish:normalize()
    elseif flat:length() > 0.5 then
      self.dash_dir = flat:normalize()
    end
    self.dash_left = 0.18
    self.cooldown = config.DASH_COOLDOWN
  end

  if self.dash_left > 0 then
    self.dash_left = self.dash_left - dt
    target = self.dash_dir * self.dash_speed
    debug_draw.line(from, from - self.dash_dir * 1.5, vec4(1, 0.8, 0.2, 1), 0.3)
  end

  if manager.knock_time > 0 then
    target = target + manager.knock_vec * (manager.knock_time / 0.4)
  end

  local blend = math.min(1, self.acceleration * dt)
  local horizontal = vec3(velocity.x, 0, velocity.z)
  local next_velocity = horizontal + (target - horizontal) * blend
  body:set_velocity(vec3(next_velocity.x, velocity.y, next_velocity.z))
end

return Player
