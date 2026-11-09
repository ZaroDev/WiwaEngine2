-- A walking enemy: idle until the warrior is near, chases, winds up and hits. The same script is the dragon boss (boss = true): it
-- fights in three phases by health (charge; fireball volleys; wakes the dormant yetis of the arena).
-- Enemies are pre-placed and never destroyed: a dead one loses its CharacterController and shrinks to nothing, and a restart brings it back.
-- `clip_*` name clips of the model's Animator (a wrong name is logged once and ignored).
local Game = require("assets/game/scripts/lib/game.lua")

local Enemy = Script:extend{
  properties = {
    max_hp = { type = "float", default = 60, min = 1, max = 5000 },
    speed = { type = "float", default = 3, min = 0, max = 20 },
    damage = { type = "float", default = 10, min = 0, max = 500 },
    aggro = { type = "float", default = 12, min = 1, max = 100 },
    reach = { type = "float", default = 1.8, min = 0.5, max = 10 },
    group = { type = "string", default = "courtyard" },
    bar = { type = "int", default = 0, min = 0, max = 32 },
    bar_height = { type = "float", default = 2.4, min = 0, max = 10 },
    boss = { type = "bool", default = false },
    dormant = { type = "bool", default = false },
    model_scale = { type = "float", default = 1, min = 0.1, max = 10 },
    clip_idle = { type = "string", default = "" },
    clip_move = { type = "string", default = "" },
    clip_attack = { type = "string", default = "" },
    clip_death = { type = "string", default = "" },
  },
}

local function copy(p)
  return vec3(p.x, p.y, p.z)
end

function Enemy:on_init()
  self.model = self.entity.children[1]
  self.anim = Animation.animator(self.model)
  self.home = copy(self.transform.position)
  self.shape = Game.capture_shape(self.entity)
  self.hp = self.max_hp
  self.state = "idle"
  self.knock = vec3(0, 0, 0)
  self.cd = 0
  self.windup = 0
  self.dead_t = 0
  self.clip = nil
  self.phase = 1
  self.charge = 0
  self.charge_cd = 5
  self.volley_cd = 3
  self.minions_woken = false
  self.panel = UI.panel(find_entity("Hud"))
  self.bar_widget = nil
  Game.register_enemy(self)
  Game.on_restart(function() self:reset() end)
  if self.dormant then
    self:park()
  end
end

function Enemy:play(clip, loop)
  if self.anim == nil or clip == "" or clip == self.clip then
    return
  end
  self.clip = clip
  local ok, err = pcall(function() self.anim:crossfade(clip, 0.15, { loop = loop }) end)
  if not ok then
    log_warning("Enemy", self.entity.name .. ": " .. tostring(err))
  end
end

function Enemy:set_model_scale(s)
  self.model:get("Transform").scale = vec3(s, s, s)
end

-- Out of the game: no body, no size.
function Enemy:park()
  Game.cancel_teleport(self.entity)
  self.state = self.dormant and "dormant" or "dead"
  if self.entity:has("CharacterController") then
    self.entity:remove("CharacterController")
  end
  self:set_model_scale(0)
end

function Enemy:reset()
  self.hp = self.max_hp
  self.knock = vec3(0, 0, 0)
  self.cd = 0
  self.windup = 0
  self.clip = nil
  self.phase = 1
  self.charge = 0
  self.charge_cd = 5
  self.volley_cd = 3
  self.minions_woken = false
  if self.dormant then
    self.state = "dormant"
    self:park()
    return
  end
  self.state = "idle"
  self:set_model_scale(self.model_scale)
  Game.teleport(self.entity, self.home, self.shape)
end

-- Wakes a dormant enemy where it was placed.
function Enemy:wake()
  if self.state ~= "dormant" then
    return
  end
  self.state = "idle"
  self.hp = self.max_hp
  self:set_model_scale(self.model_scale)
  Game.teleport(self.entity, self.home, self.shape)
  Game.minions_awake = Game.minions_awake + 1
end

-- Phase of the boss by health.
function Enemy:update_phase()
  local fraction = self.hp / self.max_hp
  local phase = fraction > 0.66 and 1 or (fraction > 0.33 and 2 or 3)
  if phase ~= self.phase then
    self.phase = phase
    log("Boss", "phase " .. phase)
  end
  if phase >= 3 and not self.minions_woken then
    self.minions_woken = true
    for _, other in ipairs(Game.enemies) do
      if other.group == "minions" then
        other:wake()
      end
    end
  end
end

function Enemy:take_hit(amount, direction)
  if self.state == "dead" or self.state == "dormant" or Game.state ~= "playing" then
    return false
  end
  self.hp = self.hp - amount
  local push = self.boss and 1.5 or 7
  if direction ~= nil then
    self.knock = vec3(direction.x * push, 0, direction.z * push)
  end
  if self.boss then
    self:update_phase()
  end
  if self.hp <= 0 then
    self:die()
  end
  return true
end

function Enemy:die()
  self.hp = 0
  self.state = "dead"
  self.dead_t = 0
  self.knock = vec3(0, 0, 0)
  Game.cancel_teleport(self.entity)
  if self.entity:has("CharacterController") then
    self.entity:remove("CharacterController")
  end
  self:play(self.clip_death, false)
  Game.kills = Game.kills + 1
  if not self.boss then
    local p = self.transform.position
    Game.drop(vec3(p.x, p.y, p.z))
  end
  if self.boss then
    Game.boss = nil
    Game.win()
  end
end

function Enemy:update_bar()
  if self.boss then
    Game.boss = (self.state ~= "dead" and Game.zone == "arena") and { hp = self.hp, max = self.max_hp } or nil
    return
  end
  if self.bar == 0 then
    return
  end
  if self.bar_widget == nil then
    if self.panel == nil or not self.panel.ready then
      return
    end
    self.bar_widget = self.panel:find("Bar" .. self.bar)
    if self.bar_widget == nil then
      self.bar = 0
      return
    end
    self.bar_widget.world_anchor = self.entity
    self.bar_widget.world_offset = vec3(0, self.bar_height, 0)
  end
  local show = self.state ~= "dead" and self.state ~= "dormant" and self.hp < self.max_hp and Game.state == "playing"
  self.bar_widget.visible = show
  if show then
    self.bar_widget.value = self.hp / self.max_hp
  end
end

function Enemy:on_update(dt)
  self:update_bar()
  if self.state == "dead" then
    self.dead_t = self.dead_t + dt
    if self.dead_t > 3 and self.entity:is_valid() then
      self:set_model_scale(0)
    end
  end
end

function Enemy:face(direction)
  if direction:length() > 0.01 then
    self.model:get("Transform").rotation = quat.from_euler(0, Game.yaw_of(direction), 0)
  end
end

function Enemy:volley(to_player, count)
  local p = self.transform.position
  local origin = vec3(p.x, p.y + 1.8, p.z) + to_player * 1.5
  local half = (count - 1) / 2
  for i = 1, count do
    local angle = (i - 1 - half) * 14
    local rotated = quat.from_euler(0, angle, 0) * to_player
    Game.fire("boss", origin, rotated)
  end
end

function Enemy:on_physics_update(dt)
  if self.state == "dead" or self.state == "dormant" or not self.entity:has("CharacterController") then
    return
  end
  if Game.state ~= "playing" then
    character.move(self.entity, vec3(0, 0, 0))
    return
  end

  local target = Game.player_position()
  if target == nil then
    return
  end
  local here = self.transform.position
  local to = vec3(target.x - here.x, 0, target.z - here.z)
  local dist = to:length()
  local dir = dist > 0.001 and to / dist or vec3(0, 0, -1)
  self.cd = math.max(0, self.cd - dt)

  local move = vec3(0, 0, 0)
  if self.state == "idle" then
    self:play(self.clip_idle, true)
    if dist < self.aggro then
      self.state = "chase"
    end
  elseif self.state == "chase" then
    if self.charge > 0 then
      self.charge = self.charge - dt
      move = dir * (self.speed * 3.2)
      self:play(self.clip_move, true)
    elseif dist <= self.reach then
      if self.cd <= 0 then
        self.state = "attack"
        self.windup = 0.6
        self:play(self.clip_attack, false)
      else
        self:play(self.clip_idle, true)
      end
    else
      move = dir * self.speed
      self:play(self.clip_move, true)
    end
    if self.boss and self.charge <= 0 then
      self.charge_cd = self.charge_cd - dt
      if self.charge_cd <= 0 and dist > 6 then
        self.charge = 0.8
        self.charge_cd = 7
      end
      if self.phase >= 2 then
        self.volley_cd = self.volley_cd - dt
        if self.volley_cd <= 0 then
          self.volley_cd = self.phase >= 3 and 2.5 or 4
          self:volley(dir, self.phase >= 3 and 5 or 3)
        end
      end
    end
  elseif self.state == "attack" then
    self.windup = self.windup - dt
    if self.windup <= 0 then
      if dist <= self.reach * 1.4 then
        Game.damage_player(self.damage, dir)
      end
      self.cd = 1.2
      self.state = "chase"
      self.clip = nil
    end
  end

  self:face(self.state == "attack" and dir or (move:length() > 0.01 and move or dir))
  character.move(self.entity, move + self.knock)
  self.knock = self.knock * math.max(0, 1 - 8 * dt)
end

return Enemy
