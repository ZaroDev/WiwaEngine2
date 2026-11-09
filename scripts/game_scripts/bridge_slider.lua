-- A kinematic platform that slides along z over the pit; the warrior rides along.
local BridgeSlider = Script:extend{
  properties = {
    amplitude = { type = "float", default = 0.6, min = 0, max = 10 },
    speed = { type = "float", default = 0.9, min = 0, max = 5 },
  },
}

function BridgeSlider:on_init()
  local p = self.transform.position
  self.base = vec3(p.x, p.y, p.z)
  self.t = 0
end

function BridgeSlider:on_physics_update(dt)
  self.t = self.t + dt
  self.transform.position.z = self.base.z + self.amplitude * math.sin(self.t * self.speed)
end

return BridgeSlider
