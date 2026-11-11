-- Makes a noise at its own position every few seconds (AI.emit_noise): the guards of the behaviour tree demo that hear it go and look.
local NoiseMaker = Script:extend{
  properties = {
    interval = { type = "float", default = 3.0, min = 0.5, max = 30 },
    loudness = { type = "float", default = 1.0, min = 0.1, max = 10 },
  },
}

function NoiseMaker:on_init()
  self.t = 1.0
end

function NoiseMaker:on_update(dt)
  self.t = self.t - dt
  if self.t <= 0 then
    self.t = self.interval
    AI.emit_noise(self.transform.position, self.loudness, "crash")
  end
end

return NoiseMaker
