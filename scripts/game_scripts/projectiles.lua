-- A pool of pre-placed projectile entities ("<prefix> 1" .. "<prefix> count"). There is no spawn call in Lua, so a shot takes a free one,
-- moves it a step at a time with a ray (a sweep: nothing tunnels) and parks it again on a hit or when its time is up.
-- owner "player": hurts enemies. owner "boss": hurts the player and flies through enemies.
local Game = require("assets/game/scripts/lib/game.lua")

local Projectiles = Script:extend{
  properties = {
    prefix = { type = "string", default = "Projectile" },
    owner = { type = "string", default = "player" },
    count = { type = "int", default = 12, min = 1, max = 64 },
    speed = { type = "float", default = 24, min = 1, max = 100 },
    life = { type = "float", default = 2.5, min = 0.1, max = 20 },
    damage = { type = "float", default = 25, min = 0, max = 500 },
    size = { type = "float", default = 0.35, min = 0.05, max = 3 },
  },
}

local PARKED = vec3(0, -200, 0)

function Projectiles:on_init()
  self.pool = {}
  for i = 1, self.count do
    local entity = find_entity(self.prefix .. " " .. i)
    if entity ~= nil then
      self.pool[#self.pool + 1] = { entity = entity, active = false, position = vec3(0, 0, 0), direction = vec3(0, 0, -1), born = 0 }
    end
  end
  Game.shots[self.owner] = self
  Game.on_restart(function() self:clear() end)
  self:clear()
end

function Projectiles:park(shot)
  shot.active = false
  local t = shot.entity:get("Transform")
  t.position = PARKED
  t.scale = vec3(0, 0, 0)
end

function Projectiles:clear()
  for _, shot in ipairs(self.pool) do
    self:park(shot)
  end
end

function Projectiles:active_count()
  local n = 0
  for _, shot in ipairs(self.pool) do
    if shot.active then
      n = n + 1
    end
  end
  return n
end

-- Launches a projectile; false when every one of the pool is in flight.
function Projectiles:fire(origin, direction)
  for _, shot in ipairs(self.pool) do
    if not shot.active then
      shot.active = true
      shot.position = vec3(origin.x, origin.y, origin.z)
      shot.direction = direction:normalize()
      shot.born = time.total()
      local t = shot.entity:get("Transform")
      t.position = shot.position
      t.scale = vec3(self.size, self.size, self.size)
      return true
    end
  end
  return false
end

function Projectiles:on_physics_update(dt)
  if Game.state ~= "playing" then
    return
  end
  for _, shot in ipairs(self.pool) do
    if shot.active then
      local travel = self.speed * dt
      local stopped = time.total() - shot.born >= self.life
      for _, hit in ipairs(physics.raycast_all(shot.position, shot.direction, travel + self.size)) do
        local name = hit.entity.name
        if self.owner == "player" then
          if name ~= "Player" then
            Game.hit_enemy(name, self.damage, shot.direction)
            stopped = true
            break
          end
        else
          if name == "Player" then
            Game.damage_player(self.damage, shot.direction)
            stopped = true
            break
          elseif Game.by_name[name] == nil then
            stopped = true
            break
          end
        end
      end
      if stopped then
        self:park(shot)
      else
        shot.position = shot.position + shot.direction * travel
        shot.entity:get("Transform").position = shot.position
      end
    end
  end
end

return Projectiles
