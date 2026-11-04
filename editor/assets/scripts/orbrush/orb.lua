-- Orb Rush: a pickup. The manager moves it (a kinematic body with a trigger) and decides when it counts; this script spins and bobs it
-- while it is on the floor, and reports the player touching its trigger.

local session = require("assets/scripts/orbrush/lib/session.lua")

local Orb = Script:extend{
  properties = {},
}

function Orb:on_init()
  self.t = math.random() * 6
end

function Orb:on_update(dt)
  local position = self.transform.position
  if position.y < -10 then
    return
  end
  self.t = self.t + dt
  position.y = 0.8 + 0.15 * math.sin(self.t * 3)
  self.transform.rotation = quat.from_euler(0, self.t * 120, 0)
  debug_draw.sphere(vec3(position.x, 0.8, position.z), 0.6, vec4(0.2, 1, 1, 1), 0)
end

function Orb:on_trigger_enter(other)
  if other.name == "Player" and session.manager ~= nil then
    session.manager:collect(self.entity)
  end
end

return Orb
