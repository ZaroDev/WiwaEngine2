-- The animation graph API: the warrior plays a graph (idle, and a walk/run blend space on "speed", with an upper body layer for the sword
-- attack). The script only sets parameters: it runs and stands every period (it starts running), and swings the sword every attack_every seconds
-- (the layer weight "upper" follows the attack). "footstep" events of the clips are counted. With autopilot off the Move action decides
-- (the Gameplay map must be on the stack).
local AnimationGraphDemo = Script:extend{
  properties = {
    autopilot = { type = "bool", default = true },
    period = { type = "float", default = 4, min = 0.5, max = 20 },
    attack_every = { type = "float", default = 3, min = 0.5, max = 20 },
    run_speed = { type = "float", default = 3, min = 0, max = 5 },
  },
}

function AnimationGraphDemo:on_init()
  self.anim = Animation.animator(self.entity)
  self.pushed = false
  self.t = 0
  self.speed = 0
  self.upper = 0
  self.next_attack = self.attack_every
  self.attack_until = 0
  self.attack_pending = false
  self.steps = 0
  if self.anim == nil then
    return
  end
  self.anim:on("footstep", function(me, event)
    me.steps = me.steps + 1
  end)
end

function AnimationGraphDemo:on_update(dt)
  if self.anim == nil then
    return
  end
  self.t = self.t + dt

  local target
  if self.autopilot then
    target = (math.floor(self.t / self.period) % 2) == 0 and self.run_speed or 0
  else
    if not self.pushed then
      self.pushed = pcall(input.push_map, "Gameplay")
    end
    local stick = self.pushed and input.action("Gameplay", "Move"):vector() or vec2(0, 0)
    target = stick:length() * self.run_speed
  end
  self.speed = self.speed + (target - self.speed) * math.min(1, dt * 20)
  self.anim:set("speed", self.speed)

  -- the sword swing: the upper body layer fades in, then the trigger starts the swing, and the layer fades out when it is over
  if self.t >= self.next_attack then
    self.next_attack = self.t + self.attack_every
    self.attack_pending = true
  end
  local want_upper = (self.attack_pending or self.t < self.attack_until) and 1 or 0
  self.upper = self.upper + (want_upper - self.upper) * math.min(1, dt * 30)
  self.anim:set("upper", self.upper)
  if self.attack_pending and self.upper > 0.9 then
    self.attack_pending = false
    self.attack_until = self.t + 0.9
    self.anim:trigger("attack")
  end
end

return AnimationGraphDemo
