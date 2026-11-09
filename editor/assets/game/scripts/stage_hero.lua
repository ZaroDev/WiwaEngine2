-- A figure of the main menu scene (a hero, the dragon over the bridge): plays one clip in a loop at a chosen size. Offscreen views cannot
-- animate yet, so the menu shows the real models standing in the scene.
local StageHero = Script:extend{
  properties = {
    clip = { type = "string", default = "" },
    size = { type = "float", default = 1, min = 0.1, max = 20 },
  },
}

function StageHero:on_init()
  self.transform.scale = vec3(self.size, self.size, self.size)
  local anim = Animation.animator(self.entity)
  if anim ~= nil and self.clip ~= "" then
    anim:play(self.clip, { loop = true })
  end
end

return StageHero
