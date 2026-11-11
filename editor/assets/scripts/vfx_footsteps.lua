-- Particles from animation events: every "footstep" event of the entity's animator kicks up a puff of dust at its feet (Vfx.spawn).
local VfxFootsteps = Script:extend{
  properties = {
    spread = { type = "float", default = 0.18, min = 0, max = 1 },
  },
}

function VfxFootsteps:on_init()
  self.left = true
  self.effect = load_asset("assets/vfx/footstep_dust.wfx")
  local anim = Animation.animator(self.entity)
  if anim ~= nil then
    anim:on("footstep", function(me, event)
      me:puff()
    end)
  end
end

function VfxFootsteps:puff()
  local p = self.transform.position
  local side = self.left and -self.spread or self.spread
  self.left = not self.left
  Vfx.spawn{ effect = self.effect, position = vec3(p.x + side, 0.05, p.z), name = "Footstep dust" }
end

return VfxFootsteps
