-- Orb Rush: a red bouncer. Every few seconds it kicks itself in a random direction so it keeps roaming; touching the player throws the
-- player back and breaks the combo. Velocities are set rather than forces applied, so the mass does not matter.

local session = require("assets/scripts/orbrush/lib/session.lua")

local Hazard = Script:extend{
  properties = {
    speed = { type = "float", default = 5, min = 0, max = 20 },
    interval = { type = "float", default = 2.5, min = 0.5, max = 10 },
  },
}

function Hazard:on_init()
  self.t = math.random() * self.interval
  self.hit_cooldown = 0
end

function Hazard:on_physics_update(dt)
  local manager = session.manager
  if manager == nil or manager.state ~= "playing" then
    return
  end
  self.hit_cooldown = math.max(0, self.hit_cooldown - dt)
  self.t = self.t + dt
  if self.t >= self.interval then
    self.t = 0
    local angle = math.random() * 6.2832
    local body = self.entity:body()
    local velocity = body:get_velocity() or vec3(0, 0, 0)
    body:set_velocity(vec3(math.cos(angle) * self.speed, velocity.y, math.sin(angle) * self.speed))
  end
end

function Hazard:on_collision_enter(other, contact)
  if other.name ~= "Player" or self.hit_cooldown > 0 then
    return
  end
  local manager = session.manager
  if manager == nil then
    return
  end
  -- the normal points from the player towards this hazard: the player goes the other way, flat
  local away = vec3(-contact.normal.x, 0, -contact.normal.z)
  if away:length() < 0.01 then
    return
  end
  if manager:knock(away:normalize()) then
    self.hit_cooldown = 0.5
    debug_draw.sphere(contact.point, 0.5, vec4(1, 0.2, 0.1, 1), 0.4)
  end
end

return Hazard
