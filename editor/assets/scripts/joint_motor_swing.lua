-- Joints demo: drives the entity's hinge or slider with a motor, back and forth: the direction flips every `period` seconds.
-- The Joint component has the axis and the limits; this only sets the motor (joint.set_motor), one physics step later.
local Swing = Script:extend{
  properties = {
    speed = { type = "float", default = 1, min = 0, max = 20 },
    period = { type = "float", default = 3, min = 0.2, max = 30 },
    max_force = { type = "float", default = 2000, min = 0, max = 100000 },
  },
}

function Swing:on_init()
  self.t = 0
  self.direction = 1
  self.started = false
end

function Swing:on_physics_update(dt)
  if not self.started then
    self.started = true
    joint.set_motor(self.entity, "velocity", self.direction * self.speed, self.max_force)
  end
  self.t = self.t + dt
  if self.t >= self.period then
    self.t = 0
    self.direction = -self.direction
    joint.set_motor(self.entity, "velocity", self.direction * self.speed, self.max_force)
  end
end

return Swing
