local wave = require("assets/scripts/lib/wave.lua")

local LightPulse = Script:extend{
  properties = {
    amplitude = { type = "float", default = 2, min = 0, max = 20 },
    speed = { type = "float", default = 6, min = 0, max = 30 },
  },
}

function LightPulse:on_init()
  self.base = self.entity:get("Light").intensity
  self.t = 0
end

function LightPulse:on_update(dt)
  self.t = self.t + dt
  self.entity:get("Light").intensity = self.base + self.amplitude * wave.sine(self.t * self.speed)
end

return LightPulse
