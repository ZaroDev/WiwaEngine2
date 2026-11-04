-- Character demo: a character walks, jumps, climbs the low step and the walkable ramp, and is stopped by the tall step and the steep ramp.
-- "Hero" is driven by the Gameplay actions (WASD or the left stick to walk, Space or the pad's A to jump), the same ones the Input demo ball
-- uses, so both move while the Viewport has the focus. "Patrol" has `autopilot` on: it walks to one end of its lane, jumps, and comes back.
-- Everything physical is the CharacterController on the entity; this script only says where to go (character.move) and when to jump
-- (character.jump), one physics step at a time.

local CharacterDemo = Script:extend{
  properties = {
    speed = { type = "float", default = 4, min = 0, max = 20 },
    jump_speed = { type = "float", default = 6, min = 0, max = 20 },
    autopilot = { type = "bool", default = false },
    leg = { type = "float", default = 4, min = 0.5, max = 60 },
  },
}

function CharacterDemo:on_init()
  self.pushed = false
  self.want_jump = false
  self.t = 0
  self.direction = 1
end

function CharacterDemo:on_update(dt)
  if self.autopilot then
    return
  end
  -- the Gameplay map is in the project's input map, which loads in the background: push it as soon as it is there
  if not self.pushed then
    self.pushed = pcall(input.push_map, "Gameplay")
    if not self.pushed then
      return
    end
  end
  -- a press lasts one frame and a frame is shorter than a physics step: it is kept until the next step uses it
  if input.action("Gameplay", "Jump"):pressed() then
    self.want_jump = true
  end
end

function CharacterDemo:on_physics_update(dt)
  local move
  if self.autopilot then
    self.t = self.t + dt
    if self.t >= self.leg then
      self.t = 0
      self.direction = -self.direction
      self.want_jump = true
    end
    move = vec3(self.direction * self.speed, 0, 0)
  else
    if not self.pushed then
      return
    end
    local stick = input.action("Gameplay", "Move"):vector()
    move = vec3(stick.x * self.speed, 0, -stick.y * self.speed)
  end

  character.move(self.entity, move)
  if self.want_jump then
    self.want_jump = false
    if character.is_grounded(self.entity) then
      character.jump(self.entity, self.jump_speed)
    end
  end
end

return CharacterDemo
