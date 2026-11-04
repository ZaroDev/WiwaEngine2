-- Joints demo: flies sideways at the start to hit the box that hangs by a weak joint (see joint_breaker.lua).
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
