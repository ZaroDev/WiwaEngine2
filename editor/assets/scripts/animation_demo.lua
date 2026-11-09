-- The Animation API: plays the idle clip and crossfades between it and the run clip every period; "footstep" events are counted.
-- With autopilot off the Move action decides (the Gameplay map must be on the stack).
local AnimationDemo = Script:extend{
  properties = {
    autopilot = { type = "bool", default = true },
    period = { type = "float", default = 3, min = 0.5, max = 20 },
    fade = { type = "float", default = 0.2, min = 0, max = 2 },
    idle = { type = "string", default = "CharacterArmature|Idle" },
    run = { type = "string", default = "CharacterArmature|Run" },
  },
}

function AnimationDemo:on_init()
  self.anim = Animation.animator(self.entity)
  self.running = false
  self.pushed = false
  self.t = 0
  self.steps = 0
  if self.anim == nil then
    return
  end
  self.anim:on("footstep", function(me, event)
    me.steps = me.steps + 1
  end)
  self.anim:play(self.idle, { loop = true })
end

function AnimationDemo:on_update(dt)
  if self.anim == nil then
    return
  end

  local want_run
  if self.autopilot then
    self.t = self.t + dt
    want_run = (math.floor(self.t / self.period) % 2) == 1
  else
    if not self.pushed then
      self.pushed = pcall(input.push_map, "Gameplay")
    end
    local stick = self.pushed and input.action("Gameplay", "Move"):vector() or vec2(0, 0)
    want_run = stick:length() > 0.2
  end

  if want_run ~= self.running then
    self.running = want_run
    self.anim:crossfade(want_run and self.run or self.idle, self.fade, { loop = true })
  end
end

return AnimationDemo
