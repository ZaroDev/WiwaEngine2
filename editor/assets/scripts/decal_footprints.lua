-- Decals from a script: every "footstep" event of the entity's animator leaves a footprint (Decal.spawn) that fades out on its own, and the
-- mesh of the character does not receive decals itself (MeshRenderer.ReceivesDecals).
local DecalFootprints = Script:extend{
  properties = {
    lifetime = { type = "float", default = 4, min = 0.5, max = 30 },
  },
}

local function stop_receiving(entity)
  for _, child in ipairs(entity.children) do
    if child:has("MeshRenderer") then
      child:get("MeshRenderer").receivesdecals = false
    end
    stop_receiving(child)
  end
end

function DecalFootprints:on_init()
  self.left = true
  self.material = load_asset("assets/materials/decal_footprint.wmat")
  stop_receiving(self.entity)
  local anim = Animation.animator(self.entity)
  if anim ~= nil then
    anim:on("footstep", function(me, event)
      me:step()
    end)
  end
end

function DecalFootprints:step()
  local p = self.transform.position
  local side = self.left and -0.18 or 0.18
  self.left = not self.left
  Decal.spawn{ material = self.material, position = vec3(p.x + side, 0.02, p.z), normal = vec3(0, 1, 0), size = vec3(0.3, 0.5, 0.4),
    lifetime = self.lifetime, fade_in = 0.1, fade_out = 1.5, roll = 180, sort_order = 5, name = "Footprint" }
end

return DecalFootprints
