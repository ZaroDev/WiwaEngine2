-- Particles from a script: plays a one-shot effect (Vfx.spawn) at its own position every few seconds; the entity that spawned it is removed by itself
-- when the effect is over. A placed ParticleSystem (the campfire, the fountain) needs no script.
local VfxDemo = Script:extend{
  properties = {
    interval = { type = "float", default = 2.5, min = 0.2, max = 30 },
  },
}

function VfxDemo:on_init()
  self.effect = load_asset("assets/vfx/impact_sparks.wfx")
  self.t = self.interval
end

function VfxDemo:on_update(dt)
  self.t = self.t - dt
  if self.t <= 0 then
    self.t = self.interval
    Vfx.spawn{ effect = self.effect, position = self.transform.position, name = "Impact sparks" }
  end
end

return VfxDemo
