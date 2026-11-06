-- A trigger collider: lights the Light of its entity while something is inside.
local TriggerZone = Script:extend{
  properties = {
    glow = { type = "float", default = 25, min = 0, max = 200 },
  },
}

function TriggerZone:on_init()
  self.inside = 0
end

function TriggerZone:on_trigger_enter(other)
  self.inside = self.inside + 1
  self.entity:get("Light").intensity = self.glow
  log("Physics", other.name .. " entered " .. self.name)
end

function TriggerZone:on_trigger_exit(other)
  self.inside = math.max(0, self.inside - 1)
  if self.inside == 0 then
    self.entity:get("Light").intensity = 0
  end
end

return TriggerZone
