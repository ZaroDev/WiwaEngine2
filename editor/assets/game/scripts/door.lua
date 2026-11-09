-- A door that stays shut until every enemy of its group is dead (and, for the gate of the arena, until the arena scene has loaded).
-- It is a kinematic body: it sinks into the ground by writing its transform, like the sliding platform. A restart shuts it again.
local Game = require("assets/game/scripts/lib/game.lua")

local Door = Script:extend{
  properties = {
    group = { type = "string", default = "courtyard" },
    needs_arena = { type = "bool", default = false },
    drop = { type = "float", default = 7, min = 0, max = 30 },
    speed = { type = "float", default = 4, min = 0.1, max = 30 },
  },
}

function Door:on_init()
  self.home_y = self.transform.position.y
  self.open = false
  Game.doors[#Game.doors + 1] = self
  Game.on_restart(function() self.open = false end)
end

function Door:wanted()
  if Game.state ~= "playing" or #Game.enemies == 0 then
    return false
  end
  if Game.alive_in_group(self.group) > 0 then
    return false
  end
  return not self.needs_arena or Game.boss_ready
end

function Door:on_update(dt)
  if not self.open and self:wanted() then
    self.open = true
    log("Door", self.name .. " opens")
  end
  local target = self.open and (self.home_y - self.drop) or self.home_y
  local t = self.transform
  local y = t.position.y
  if y ~= target then
    local step = self.speed * dt
    t.position.y = y > target and math.max(target, y - step) or math.min(target, y + step)
  end
end

return Door
