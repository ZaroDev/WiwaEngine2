-- Orb Rush: turns the props behind the main menu.

local Showcase = Script:extend{
  properties = {
    degrees_per_second = { type = "float", default = 25, min = -360, max = 360 },
  },
}

function Showcase:on_init()
  self.angle = 0
end

function Showcase:on_update(dt)
  self.angle = self.angle + self.degrees_per_second * dt
  self.transform.rotation = quat.from_euler(0, self.angle, 0)
end

return Showcase
