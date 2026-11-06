-- A CharacterController walked with the Move action, or (autopilot) back and forth along its lane with a jump at each turn.
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
  if not self.pushed then
    self.pushed = pcall(input.push_map, "Gameplay")
    if not self.pushed then
      return
    end
  end
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
