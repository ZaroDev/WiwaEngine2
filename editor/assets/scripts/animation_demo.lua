-- Animation demo: the fox crossfades between its idle clip (Survey) and its run clip (Run). It runs while the Gameplay Move action is held
-- (WASD or the left stick, the same action the Input demo uses); with `autopilot` on it alternates by itself, so that the demo shows both
-- without input. "footstep" is an event on the Run clip (set in the model's import settings), counted in `steps` for the demo smoke test.
-- The prop that follows the fox's head is an AnimationSocket on a child entity: no script involved.

local AnimationDemo = Script:extend{
  properties = {
    autopilot = { type = "bool", default = true },
    period = { type = "float", default = 3, min = 0.5, max = 20 },
    fade = { type = "float", default = 0.2, min = 0, max = 2 },
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
  self.anim:play("Survey", { loop = true })
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
    self.anim:crossfade(want_run and "Run" or "Survey", self.fade, { loop = true })
  end
end

return AnimationDemo
