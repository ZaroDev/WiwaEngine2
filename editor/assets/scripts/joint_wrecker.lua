-- Launches its body once with set_velocity.
local Wrecker = Script:extend{
  properties = {
    velocity = { type = "vec3", default = { -10, 0, 0 } },
  },
}

function Wrecker:on_init()
  self.launched = false
end

function Wrecker:on_physics_update(dt)
  if not self.launched then
    self.launched = true
    self.entity:body():set_velocity(self.velocity)
  end
end

return Wrecker
