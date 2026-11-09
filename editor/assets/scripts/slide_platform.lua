-- Moves a kinematic body along x with the physics step; characters standing on it ride along.
local SlidePlatform = Script:extend{
  properties = {
    amplitude = { type = "float", default = 2.5, min = 0, max = 20 },
    speed = { type = "float", default = 0.6, min = 0, max = 5 },
  },
}

function SlidePlatform:on_init()
  self.base_x = self.transform.position.x
  self.t = 0
end

function SlidePlatform:on_physics_update(dt)
  self.t = self.t + dt
  self.transform.position.x = self.base_x + self.amplitude * math.sin(self.t * self.speed)
end

return SlidePlatform
