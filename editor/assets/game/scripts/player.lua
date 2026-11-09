-- The hero: walks with Move (relative to the camera), jumps, and attacks with Fire or Melee: the Warrior swings his sword (stamina), the Wizard
-- shoots bolts (mana). The models are children of this entity; the unused hero is scaled to nothing. Models face +z.
-- The Warrior's animation graph takes "speed", the "upper" layer weight and the "attack" trigger; the Wizard crossfades clips.
local Game = require("assets/game/scripts/lib/game.lua")

local Player = Script:extend{
  properties = {
    speed = { type = "float", default = 6, min = 0, max = 20 },
    jump_speed = { type = "float", default = 8, min = 0, max = 20 },
    melee_damage = { type = "float", default = 30, min = 0, max = 500 },
    shot_cooldown = { type = "float", default = 0.4, min = 0.05, max = 5 },
    run_blend = { type = "float", default = 3, min = 0, max = 5 },
    wizard_idle = { type = "string", default = "" },
    wizard_walk = { type = "string", default = "" },
    wizard_attack = { type = "string", default = "" },
  },
}

local SWING_TIME = 0.8
local HIT_TIME = 0.3

function Player:on_init()
  self.models = {}
  for _, child in ipairs(self.entity.children) do
    self.models[child.name] = child
  end
  self.applied = nil
  self.model = nil
  self.anim = nil
  self.pushed = false
  self.want_jump = false
  self.facing = vec3(0, 0, 1)
  self.knock = vec3(0, 0, 0)
  self.swing = 0            -- seconds left of the sword swing
  self.swing_hit = false
  self.shot_cd = 0
  self.cast = 0             -- seconds left of the Wizard's cast pose
  self.clip = nil
  self.upper = 0
  self.blend = 0
  Game.player = self.entity
  Game.player_script = self
end

-- Shows the model of the selected hero (once, when the selection changes).
function Player:apply_hero()
  if self.applied == Game.hero then
    return
  end
  self.applied = Game.hero
  for name, entity in pairs(self.models) do
    local on = (name == Game.hero_def().model)
    entity:get("Transform").scale = on and vec3(1, 1, 1) or vec3(0, 0, 0)
    if name == Game.hero_def().model then
      self.model = entity
      self.anim = Animation.animator(entity)
    end
  end
  self.clip = nil
  Game.resource = Game.hero_def().max
end

function Player:knock_back(direction)
  self.knock = vec3(direction.x * 7, 0, direction.z * 7)
end

function Player:play_clip(clip, loop)
  if self.anim == nil or clip == "" or clip == self.clip then
    return
  end
  self.clip = clip
  local ok, err = pcall(function() self.anim:crossfade(clip, 0.15, { loop = loop }) end)
  if not ok then
    log_warning("Player", tostring(err))
  end
end

-- The Warrior's attack. False if one is going on or the stamina is short.
function Player:start_swing()
  if self.swing > 0 or Game.hero_def().kind ~= "melee" then
    return false
  end
  if not Game.spend_resource(Game.hero_def().cost) then
    return false
  end
  self.swing = SWING_TIME
  self.swing_hit = false
  return true
end

-- The Wizard's attack. False if the bolt is not ready, the mana is short or every bolt is in flight.
function Player:shoot()
  if self.shot_cd > 0 or Game.hero_def().kind ~= "ranged" then
    return false
  end
  if not Game.spend_resource(Game.hero_def().cost) then
    return false
  end
  local p = self.transform.position
  local origin = vec3(p.x, p.y + 1.1, p.z) + self.facing * 0.8
  if not Game.fire("player", origin, self.facing) then
    Game.add_resource(Game.hero_def().cost)
    return false
  end
  self.shot_cd = self.shot_cooldown
  self.cast = 0.5
  return true
end

function Player:attack()
  if Game.hero_def().kind == "melee" then
    return self:start_swing()
  end
  return self:shoot()
end

function Player:do_hit()
  local p = self.transform.position
  local center = vec3(p.x, p.y + 0.9, p.z) + self.facing * 1.2
  local seen = {}
  for _, other in ipairs(physics.overlap_sphere(center, 1.6)) do
    local name = other.name
    if name ~= "Player" and not seen[name] then
      seen[name] = true
      Game.hit_enemy(name, self.melee_damage, self.facing)
    end
  end
  debug_draw.sphere(center, 1.6, vec4(1, 0.8, 0.2, 1), 0.15)
end

-- The camera's forward direction on the ground plane.
function Player:camera_forward()
  local camera = find_entity("Camera")
  if camera == nil then
    return vec3(0, 0, -1)
  end
  local f = camera:get("Transform").rotation * vec3(0, 0, -1)
  local flat = vec3(f.x, 0, f.z)
  if flat:length() < 0.001 then
    return vec3(0, 0, -1)
  end
  return flat:normalize()
end

function Player:read_move()
  if Game.fake_move ~= nil then
    return Game.fake_move
  end
  if not self.pushed then
    return vec2(0, 0)
  end
  return input.action("Gameplay", "Move"):vector()
end

function Player:on_update(dt)
  self:apply_hero()
  if not self.pushed then
    self.pushed = pcall(input.push_map, "Gameplay")
  end

  local hero = Game.hero_def()
  if Game.state == "playing" then
    Game.invuln = math.max(0, Game.invuln - dt)
    self.shot_cd = math.max(0, self.shot_cd - dt)
    self.cast = math.max(0, self.cast - dt)
    Game.resource_idle = Game.resource_idle + dt
    if Game.resource_idle > hero.regen_delay then
      Game.add_resource(hero.regen * dt)
    end
    Game.resource_flash = math.max(0, Game.resource_flash - dt)
  end
  Game.shot_fraction = Game.resource / hero.max

  if Game.state == "playing" and self.pushed then
    if input.action("Gameplay", "Jump"):pressed() then
      self.want_jump = true
    end
    if input.action("Gameplay", "Melee"):pressed() or input.action("Gameplay", "Fire"):pressed() then
      if hero.kind == "ranged" then
        self.facing = self:camera_forward()
      end
      self:attack()
    end
  end

  if self.swing > 0 and Game.state == "playing" then
    self.swing = self.swing - dt
    if not self.swing_hit and self.swing <= SWING_TIME - HIT_TIME then
      self.swing_hit = true
      self:do_hit()
    end
  end

  if hero.kind == "melee" and self.anim ~= nil then
    local want_upper = self.swing > 0 and 1 or 0
    self.upper = self.upper + (want_upper - self.upper) * math.min(1, dt * 30)
    self.anim:set("upper", self.upper)
    if self.swing > 0 and self.swing > SWING_TIME - dt * 1.5 then
      self.anim:trigger("attack")
    end
  elseif hero.kind == "ranged" then
    if self.cast > 0 then
      self:play_clip(self.wizard_attack, false)
    elseif self.blend > 0.2 then
      self:play_clip(self.wizard_walk, true)
    else
      self:play_clip(self.wizard_idle, true)
    end
  end
end

function Player:on_physics_update(dt)
  if not self.entity:has("CharacterController") then
    return
  end

  local velocity = vec3(0, 0, 0)
  if Game.state == "playing" then
    local stick = self:read_move()
    local forward = self:camera_forward()
    local right = vec3(-forward.z, 0, forward.x)
    local wish = right * stick.x + forward * stick.y
    local scale = self.swing > 0 and 0.4 or 1
    velocity = wish * (self.speed * scale)
    if wish:length() > 0.1 then
      self.facing = wish:normalize()
    end
    self.blend = math.min(1, stick:length()) * self.run_blend
  else
    self.blend = 0
  end

  velocity = velocity + self.knock
  self.knock = self.knock * math.max(0, 1 - 8 * dt)

  character.move(self.entity, velocity)
  if self.want_jump then
    self.want_jump = false
    if Game.state == "playing" and character.is_grounded(self.entity) then
      character.jump(self.entity, self.jump_speed)
    end
  end

  if self.model ~= nil then
    self.model:get("Transform").rotation = quat.from_euler(0, Game.yaw_of(self.facing), 0)
  end
  if self.anim ~= nil and Game.hero_def().kind == "melee" then
    self.anim:set("speed", self.blend)
  end
end

return Player
