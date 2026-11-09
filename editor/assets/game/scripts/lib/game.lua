-- Shared state of the demo game. `require` runs a module once, so every script that requires this file talks to the same table.
-- game_state.lua owns the flow (menus, HUD); the other scripts report to it through the functions below.
local Game = {}

local CC_FIELDS = { "Radius", "Height", "Offset", "StepHeight", "MaxSlopeAngle", "Mass", "PushStrength", "GravityFactor", "Layer" }

local function copy_value(value)
  if type(value) == "userdata" then
    local ok, copy = pcall(function() return vec3(value.x, value.y, value.z) end)
    if ok then
      return copy
    end
  end
  return value
end

local atan2 = math.atan2 or math.atan

-- The yaw in degrees that turns a model (the models face +z) towards a ground direction.
function Game.yaw_of(direction)
  return math.deg(atan2(direction.x, direction.z))
end

-- The two heroes. `model` names the child of the Player entity that shows it.
Game.heroes = {
  warrior = { name = "Warrior", model = "Warrior model", kind = "melee", resource = "Stamina", max = 100, regen = 30, regen_delay = 0.6, cost = 25,
              color = { 0.95, 0.75, 0.2, 1 }, portrait = "assets/models/characters/Warrior.fbx" },
  wizard = { name = "Wizard", model = "Wizard model", kind = "ranged", resource = "Mana", max = 100, regen = 10, regen_delay = 0.3, cost = 20,
             color = { 0.35, 0.55, 1, 1 }, portrait = "assets/models/characters/Wizard.fbx" },
}

-- Difficulty: damage taken, and the share of drops that heal (the rest restore the resource).
Game.difficulties = {
  { name = "Easy", damage = 0.6, heart_share = 0.75 },
  { name = "Normal", damage = 1.0, heart_share = 0.6 },
  { name = "Hard", damage = 1.5, heart_share = 0.4 },
}

-- The stage of the menus: the two heroes stand at `center`. The hero select camera is `camera` at `pitch`; the title camera moves on an arc of
-- +-`arc` degrees round the center, `radius` away and `height` above it, turned `shift` degrees to the left.
Game.stage = { center = vec3(0, 1.1, 2), camera = vec3(0, 1.6, 6.4), pitch = -6.5, radius = 6.6, height = 1.5, arc = 28, shift = 11 }

-- The pitch in degrees (up is positive) of a direction that rises `dy` over `horizontal` metres.
function Game.pitch_of(dy, horizontal)
  return math.deg(atan2(dy, horizontal))
end

local DEFAULT_OPTIONS = { fov = 60, sensitivity = 1.0, invert_y = false, difficulty = 2, camera_distance = 5.5, hints = true }

function Game.default_options()
  local copy = {}
  for key, value in pairs(DEFAULT_OPTIONS) do
    copy[key] = value
  end
  return copy
end

function Game.reset()
  Game.state = "title"          -- title, playing, paused, dead, won
  Game.max_hp = 100
  Game.hp = 100
  Game.invuln = 0
  Game.god = false              -- the self test cannot die by accident
  Game.zone = "courtyard"
  Game.kills = 0
  Game.wins = 0
  Game.broken_joints = 0
  Game.boss_ready = false       -- the arena scene finished loading
  Game.minions_awake = 0
  Game.boss = nil               -- the dragon while the fight is on: { hp, max }
  Game.enemies = {}
  Game.by_name = {}
  Game.restart_hooks = {}
  Game.pending = {}             -- teleports in flight
  Game.shots = {}               -- the projectile managers, by owner
  Game.player = nil
  Game.player_script = nil
  Game.spawn = nil
  Game.checkpoint = nil
  Game.shot_fraction = 1        -- 0 empty, 1 full: the resource bar
  Game.hero = "warrior"
  Game.resource = 100           -- stamina or mana, by hero
  Game.resource_idle = 0        -- seconds since the resource was spent (it regenerates after a delay)
  Game.resource_flash = 0       -- a refused attack: the HUD bar flashes
  Game.drops = {}               -- the drop pools, by kind ("health", "resource")
  Game.doors = {}
  Game.opt = Game.default_options()
  Game.ui_missing = 0           -- widgets the menus looked for and did not find
  Game.fake_move = nil          -- the self test walks the player with this
end

Game.reset()

function Game.playing()
  return Game.state == "playing"
end

function Game.set_state(state)
  if Game.state ~= state then
    log("Game", "state " .. Game.state .. " -> " .. state)
    Game.state = state
  end
end

function Game.player_position()
  local player = Game.player
  if player ~= nil and player:is_valid() then
    local p = player:get("Transform").position
    return vec3(p.x, p.y, p.z)
  end
  return nil
end

function Game.on_restart(fn)
  Game.restart_hooks[#Game.restart_hooks + 1] = fn
end

function Game.register_enemy(instance)
  Game.enemies[#Game.enemies + 1] = instance
  Game.by_name[instance.entity.name] = instance
end

-- Damages the enemy called `name`; true if it was one and took the hit.
function Game.hit_enemy(name, damage, direction)
  local enemy = Game.by_name[name]
  if enemy ~= nil then
    return enemy:take_hit(damage, direction)
  end
  return false
end

function Game.alive_in_group(group)
  local count = 0
  for _, enemy in ipairs(Game.enemies) do
    if enemy.group == group and enemy.state ~= "dead" and enemy.state ~= "dormant" then
      count = count + 1
    end
  end
  return count
end

function Game.hero_def()
  return Game.heroes[Game.hero]
end

function Game.diff()
  return Game.difficulties[Game.opt.difficulty] or Game.difficulties[2]
end

function Game.select_hero(key)
  if Game.heroes[key] ~= nil then
    Game.hero = key
    Game.resource = Game.heroes[key].max
  end
end

-- Pays for an attack; false (and the bar flashes) when there is not enough.
function Game.spend_resource(cost)
  if Game.resource < cost then
    Game.resource_flash = 0.4
    return false
  end
  Game.resource = Game.resource - cost
  Game.resource_idle = 0
  return true
end

function Game.add_resource(amount)
  Game.resource = math.min(Game.hero_def().max, Game.resource + amount)
end

function Game.heal(amount)
  Game.hp = math.min(Game.max_hp, Game.hp + amount)
end

-- An enemy died at `position`: a health orb or a resource orb may be left behind.
function Game.drop(position)
  local kind = math.random() < Game.diff().heart_share and "health" or "resource"
  return Game.spawn_drop(kind, position)
end

function Game.spawn_drop(kind, position)
  local manager = Game.drops[kind]
  return manager ~= nil and manager:spawn(position)
end

function Game.damage_player(amount, direction)
  if Game.state ~= "playing" or Game.god or Game.invuln > 0 then
    return
  end
  Game.hp = math.max(0, Game.hp - amount * Game.diff().damage)
  Game.invuln = 0.5
  if Game.player_script ~= nil and direction ~= nil then
    Game.player_script:knock_back(direction)
  end
  if Game.hp <= 0 then
    Game.set_state("dead")
  end
end

function Game.set_checkpoint(position)
  Game.checkpoint = vec3(position.x, position.y, position.z)
  log("Game", "checkpoint " .. string.format("%.1f %.1f %.1f", position.x, position.y, position.z))
end

function Game.win()
  if Game.state == "won" then
    return
  end
  Game.wins = Game.wins + 1
  Game.set_state("won")
end

-- Moves a character. Its CharacterController is taken away for a frame and put back after the move, because the physics world only places
-- a character where its component is made: the first frame removes it, the next sets the position and adds it again.
function Game.teleport(entity, position, shape, after)
  for _, job in ipairs(Game.pending) do
    if job.entity.guid == entity.guid then
      job.position = vec3(position.x, position.y, position.z)
      job.shape = shape or job.shape
      job.after = after
      return
    end
  end
  Game.pending[#Game.pending + 1] = { entity = entity, position = vec3(position.x, position.y, position.z), shape = shape, after = after, stage = 1 }
end

-- Drops a teleport in flight (an enemy that died meanwhile must not get its controller back).
function Game.cancel_teleport(entity)
  local keep = {}
  for _, job in ipairs(Game.pending) do
    if job.entity.guid ~= entity.guid then
      keep[#keep + 1] = job
    end
  end
  Game.pending = keep
end

function Game.capture_shape(entity)
  local shape = {}
  local c = entity:get("CharacterController")
  for _, field in ipairs(CC_FIELDS) do
    shape[field] = copy_value(c[field])
  end
  return shape
end

function Game.step_teleports()
  if #Game.pending == 0 then
    return
  end
  local keep = {}
  for _, job in ipairs(Game.pending) do
    local entity = job.entity
    if entity:is_valid() then
      if job.stage == 1 then
        if entity:has("CharacterController") then
          job.shape = job.shape or Game.capture_shape(entity)
          entity:remove("CharacterController")
        end
        job.stage = 2
        keep[#keep + 1] = job
      else
        entity:get("Transform").position = job.position
        if job.shape ~= nil then
          entity:add("CharacterController")
          local c = entity:get("CharacterController")
          for field, value in pairs(job.shape) do
            c[field] = value
          end
        end
        if job.after ~= nil then
          job.after()
        end
      end
    end
  end
  Game.pending = keep
end

function Game.teleporting(entity)
  for _, job in ipairs(Game.pending) do
    if job.entity.guid == entity.guid then
      return true
    end
  end
  return false
end

-- Heals and returns the player to the last checkpoint.
function Game.respawn(keep_hp)
  if not keep_hp then
    Game.hp = Game.max_hp
  end
  Game.invuln = 1.5
  local target = Game.checkpoint or Game.spawn
  if Game.player ~= nil and target ~= nil then
    Game.teleport(Game.player, target)
  end
  Game.set_state("playing")
end

-- Everything back to the start of the level: enemies, projectiles, the boss, the checkpoint.
function Game.restart()
  for _, hook in ipairs(Game.restart_hooks) do
    hook()
  end
  Game.checkpoint = Game.spawn
  Game.kills = 0
  Game.resource = Game.hero_def().max
  Game.resource_idle = 0
  Game.boss = nil
  Game.minions_awake = 0
  Game.zone = "courtyard"
  Game.respawn()
end

function Game.start()
  Game.restart()
end

function Game.fire(owner, origin, direction)
  local manager = Game.shots[owner]
  if manager == nil then
    return false
  end
  return manager:fire(origin, direction)
end

local function gate_text(group, after)
  local left = Game.alive_in_group(group)
  if left > 0 then
    return "Defeat all enemies to open the gate: " .. left .. " left"
  end
  return after
end

function Game.objective()
  if Game.zone == "arena" then
    return "Defeat the dragon"
  elseif Game.zone == "approach" then
    return gate_text("approach", Game.boss_ready and "Enter the arena" or "Step up to the gate")
  elseif Game.zone == "bridge" then
    return "Cross the bridge"
  end
  return gate_text("courtyard", "Cross the bridge to the north")
end

return Game
