-- A pool of pickup orbs ("<prefix> 1" .. "<prefix> count"), parked until an enemy dies. kind "health" heals the hero, "resource" restores
-- stamina or mana. An orb spins and bobs where it fell, is picked up when the hero comes near, and disappears after `life` seconds.
local Game = require("assets/game/scripts/lib/game.lua")

local Drops = Script:extend{
  properties = {
    prefix = { type = "string", default = "Heart" },
    kind = { type = "string", default = "health" },
    count = { type = "int", default = 6, min = 1, max = 32 },
    amount = { type = "float", default = 25, min = 0, max = 500 },
    life = { type = "float", default = 20, min = 0.1, max = 600 },
    pickup_radius = { type = "float", default = 1.4, min = 0.2, max = 5 },
    size = { type = "float", default = 0.4, min = 0.05, max = 3 },
  },
}

local PARKED = vec3(0, -200, 0)

function Drops:on_init()
  self.pool = {}
  for i = 1, self.count do
    local entity = find_entity(self.prefix .. " " .. i)
    if entity ~= nil then
      self.pool[#self.pool + 1] = { entity = entity, active = false, base = vec3(0, 0, 0), born = 0 }
    end
  end
  Game.drops[self.kind] = self
  Game.on_restart(function() self:clear() end)
  self:clear()
end

function Drops:park(drop)
  drop.active = false
  local t = drop.entity:get("Transform")
  t.position = PARKED
  t.scale = vec3(0, 0, 0)
end

function Drops:clear()
  for _, drop in ipairs(self.pool) do
    self:park(drop)
  end
end

function Drops:active_count()
  local n = 0
  for _, drop in ipairs(self.pool) do
    if drop.active then
      n = n + 1
    end
  end
  return n
end

-- Puts an orb where an enemy died; false when every orb of the pool is out.
function Drops:spawn(position)
  for _, drop in ipairs(self.pool) do
    if not drop.active then
      drop.active = true
      drop.base = vec3(position.x, position.y + 0.8, position.z)
      drop.born = time.total()
      local t = drop.entity:get("Transform")
      t.position = drop.base
      t.scale = vec3(self.size, self.size, self.size)
      return true
    end
  end
  return false
end

function Drops:collect(drop)
  if self.kind == "health" then
    Game.heal(self.amount)
  else
    Game.add_resource(self.amount)
  end
  log("Drops", self.kind .. " orb collected")
  self:park(drop)
end

function Drops:on_update(dt)
  local player = Game.player_position()
  local now = time.total()
  for _, drop in ipairs(self.pool) do
    if drop.active then
      local age = now - drop.born
      if age >= self.life then
        self:park(drop)
      else
        local t = drop.entity:get("Transform")
        t.position = vec3(drop.base.x, drop.base.y + 0.15 * math.sin(age * 3), drop.base.z)
        t.rotation = quat.from_euler(0, age * 120, 0)
        -- blink for the last seconds
        local blinking = self.life - age < 3 and math.floor(age * 6) % 2 == 0
        local s = blinking and 0 or self.size
        t.scale = vec3(s, s, s)
        if player ~= nil and Game.state == "playing" then
          local dx, dz = player.x - drop.base.x, player.z - drop.base.z
          local dy = (player.y + 0.9) - drop.base.y
          if dx * dx + dz * dz + dy * dy <= self.pickup_radius * self.pickup_radius then
            self:collect(drop)
          end
        end
      end
    end
  end
end

return Drops
