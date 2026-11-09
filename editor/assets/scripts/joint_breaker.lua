-- Hears on_joint_break when its Joint goes over its break force.
local Breaker = Script:extend{}

function Breaker:on_joint_break(other)
  log("Joints", self.name .. " broke away" .. (other and (" from " .. other.name) or ""))
  debug_draw.sphere(self.transform.position, 0.6, vec4(1, 0.3, 0.1, 1), 1.0)
end

return Breaker
