local wave = require("assets/scripts/lib/wave.lua")

local CameraSway = Script:extend{
  properties = {
    distance = { type = "float", default = 1.5, min = 0, max = 20 },
    speed = { type = "float", default = 0.4, min = 0, max = 5 },
  },
}

function CameraSway:on_init()
  self.base_x = self.transform.position.x
  self.t = 0
end

function CameraSway:on_update(dt)
  self.t = self.t + dt
  self.transform.position.x = self.base_x + self.distance * wave.sine(self.t * self.speed)
end

return CameraSway
