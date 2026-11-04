local Spinner = Script:extend{
  properties = {
    degrees_per_second = { type = "float", default = 60, min = -720, max = 720 },
    axis = { type = "vec3", default = { 0, 1, 0 } },
  },
}

function Spinner:on_init()
  self.base = self.transform.rotation
  self.angle = 0
end

function Spinner:on_update(dt)
  self.angle = self.angle + self.degrees_per_second * dt
  self.transform.rotation = self.base * quat.from_axis_angle(self.axis:normalize(), self.angle)
end

return Spinner
