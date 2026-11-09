-- Gives the arena its mood when the arena scene is loaded: it dims the sun and pulses its own red light.
local BossLight = Script:extend{
  properties = {
    amplitude = { type = "float", default = 12, min = 0, max = 100 },
    speed = { type = "float", default = 2, min = 0, max = 20 },
  },
}

function BossLight:on_init()
  self.t = 0
  self.base = self.entity:get("Light").intensity
  local sun = find_entity("Sun")
  if sun ~= nil then
    local light = sun:get("Light")
    light.intensity = light.intensity * 0.35
    light.color = vec4(1, 0.55, 0.4, 1)
  end
end

function BossLight:on_update(dt)
  self.t = self.t + dt
  self.entity:get("Light").intensity = self.base + self.amplitude * math.sin(self.t * self.speed)
end

return BossLight
