-- Bobs the entity up and down with the shared wave helper.
local wave = require("assets/scripts/lib/wave.lua")

local Bobber = Script:extend{
  properties = {
    height = { type = "float", default = 0.3, min = 0, max = 5 },
    speed = { type = "float", default = 2.5, min = 0, max = 20 },
    phase = { type = "float", default = 0, min = 0, max = 6.3 },
  },
}

function Bobber:on_init()
  self.base_y = self.transform.position.y
  self.t = 0
end

function Bobber:on_update(dt)
  self.t = self.t + dt
  self.transform.position.y = self.base_y + self.height * wave.sine(self.t * self.speed + self.phase)
end

return Bobber
