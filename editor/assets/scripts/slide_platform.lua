-- Character demo: a kinematic platform that slides back and forth along x. A character standing on it is carried along.
-- The body follows the entity: the physics sync turns a moved Transform into the platform's movement (it pushes what it meets).
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
