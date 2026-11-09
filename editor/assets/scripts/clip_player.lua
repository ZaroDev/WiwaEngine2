-- Loops one named clip of the model on its entity (the lineup of the animation station).
local ClipPlayer = Script:extend{
  properties = {
    clip = { type = "string", default = "" },
    speed = { type = "float", default = 1, min = 0, max = 4 },
  },
}

function ClipPlayer:on_init()
  local anim = Animation.animator(self.entity)
  if anim ~= nil and self.clip ~= "" then
    anim:play(self.clip, { loop = true, speed = self.speed })
  end
end

return ClipPlayer
