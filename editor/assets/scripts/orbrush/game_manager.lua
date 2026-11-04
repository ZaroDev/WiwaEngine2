-- Orb Rush: the rules of the arena, on the "Manager" entity. States: loading, playing, paused, settings, ended.
-- Restart and Play again reload the scene, so every body, orb and number starts fresh.
--
-- Other scripts reach it through session.manager (script components expose properties, not methods).

local config = require("assets/scripts/orbrush/lib/config.lua")
local session = require("assets/scripts/orbrush/lib/session.lua")
local Menu = require("assets/scripts/orbrush/lib/menu.lua")

local Manager = Script:extend{
  properties = {},
}

-- places an orb can appear (clear of the four pillars and of the hazards' starting corners)
local SPOTS = {
  { -9, -2 }, { -9, 3 }, { 9, -3 }, { 9, 2 }, { -2, -9 }, { 3, -9 }, { -3, 9 }, { 2, 9 },
  { 0, -5 }, { 0, 5 }, { -5, 0 }, { 5, 0 }, { -2.5, -2.5 }, { 2.5, -2.5 }, { -2.5, 2.5 }, { 2.5, 2.5 },
}
local ORB_Y = 0.8
local PARKED_Y = -50
local PICKUP_RADIUS = 1.05

function Manager:on_init()
  session.manager = self
  self.state = "loading"
  self.pushed = false
  self.ready = false
  self.score = 0
  self.combo = 1
  self.combo_timer = 0
  self.time_left = config.ROUND_SECONDS
  self.knock_vec = vec3(0, 0, 0)
  self.knock_time = 0
  self.tag_timer = 0
  self.spawn_timer = 0
  self.saved = {}
  self.reloading = false

  self.player = find_entity("Player")
  self.camera = find_entity("Camera")
  self.orbs = {}
  for i = 1, config.ORB_COUNT do
    self.orbs[i] = { entity = find_entity(string.format("Orb%02d", i)), active = false, spot = 0 }
  end
  self.hazards = {}
  for i = 1, config.HAZARD_COUNT do
    self.hazards[i] = find_entity("Hazard" .. i)
  end

  self.hud = UI.panel(find_entity("Hud"))
  self.pause_panel = UI.panel(find_entity("PauseMenu"))
  self.end_panel = UI.panel(find_entity("EndMenu"))
  self.settings_panel = UI.panel(find_entity("SettingsMenu"))
  self.settings = Menu.new_settings(self.settings_panel, self)
  Menu.apply_fov()
end

function Manager:on_destroy()
  if self.pushed then
    pcall(input.pop_map, "Gameplay")
    self.pushed = false
  end
  if session.manager == self then
    session.manager = nil
  end
end

function Manager:panels_ready()
  return Menu.ready(self.hud) and Menu.ready(self.pause_panel) and Menu.ready(self.end_panel) and Menu.ready(self.settings_panel)
end

-- widgets are looked up once the panels have loaded
function Manager:setup()
  local hud = self.hud
  self.score_text = hud:find("Top/Score")
  self.combo_text = hud:find("Top/Combo")
  self.time_text = hud:find("Top/Time")
  self.time_bar = hud:find("TimeBar")
  self.tag = hud:find("PickupTag")
  self.tag_text = hud:find("PickupTag/Text")
  self.final_text = self.end_panel:find("Menu/Final")
  self.best_text = self.end_panel:find("Menu/Best")

  self.pause_panel:find("Menu/Resume"):on("click", function(me) me:resume() end)
  self.pause_panel:find("Menu/Restart"):on("click", function(me) me:reload(config.ARENA_SCENE) end)
  self.pause_panel:find("Menu/Settings"):on("click", function(me) me:open_settings() end)
  self.pause_panel:find("Menu/MainMenu"):on("click", function(me) me:reload(config.TITLE_SCENE) end)
  self.end_panel:find("Menu/PlayAgain"):on("click", function(me) me:reload(config.ARENA_SCENE) end)
  self.end_panel:find("Menu/MainMenu"):on("click", function(me) me:reload(config.TITLE_SCENE) end)

  self.pause_panel.visible = false
  self.end_panel.visible = false
  self.settings_panel.visible = false
  self.tag.visible = false
  self.ready = true
  self:start_round()
end

-- ---------------------------------------------------------------- rounds

function Manager:start_round()
  self.state = "playing"
  self.score = 0
  self.combo = 1
  self.combo_timer = 0
  self.time_left = config.ROUND_SECONDS
  self.spawn_timer = 0
  for _ = 1, config.ORBS_ACTIVE do
    self:spawn_orb()
  end
end

function Manager:ensure_map()
  if not self.pushed then
    self.pushed = pcall(input.push_map, "Gameplay")
  end
end

function Manager:reload(path)
  if self.reloading then
    return
  end
  self.reloading = true
  self.load = load_scene(path, "replace")
end

function Manager:end_round()
  self.state = "ended"
  self:freeze_player()
  if self.score > session.best then
    session.best = self.score
  end
  self.final_text.text = "Score " .. self.score
  self.best_text.text = "Best " .. session.best
  self.tag.visible = false
  self.end_panel.visible = true
end

function Manager:freeze_player()
  self.player:body():set_velocity(vec3(0, 0, 0))
  self.player:body():set_angular_velocity(vec3(0, 0, 0))
end

-- ---------------------------------------------------------------- pause

-- the bodies keep no motion while paused: velocities are written down, zeroed, and given back on resume
function Manager:pause()
  if self.state ~= "playing" then
    return
  end
  self.state = "paused"
  self.saved = {}
  local bodies = { self.player }
  for _, hazard in ipairs(self.hazards) do
    bodies[#bodies + 1] = hazard
  end
  for _, entity in ipairs(bodies) do
    local body = entity:body()
    self.saved[#self.saved + 1] = { entity = entity, v = body:get_velocity(), w = body:get_angular_velocity() }
    body:set_velocity(vec3(0, 0, 0))
    body:set_angular_velocity(vec3(0, 0, 0))
  end
  physics.set_gravity(vec3(0, 0, 0))
  self.pause_panel.visible = true
end

function Manager:resume()
  if self.state ~= "paused" then
    return
  end
  physics.set_gravity(vec3(0, config.GRAVITY_Y, 0))
  for _, saved in ipairs(self.saved) do
    local body = saved.entity:body()
    if saved.v ~= nil then
      body:set_velocity(saved.v)
    end
    if saved.w ~= nil then
      body:set_angular_velocity(saved.w)
    end
  end
  self.saved = {}
  self.pause_panel.visible = false
  self.state = "playing"
end

function Manager:open_settings()
  if self.state ~= "paused" then
    return
  end
  self.state = "settings"
  self.pause_panel.visible = false
  self.settings_panel.visible = true
  self.settings:open()
end

function Manager:on_settings_closed()
  self.settings_panel.visible = false
  self.pause_panel.visible = true
  self.state = "paused"
end

-- ---------------------------------------------------------------- orbs and scoring

function Manager:spot_taken(index)
  for _, orb in ipairs(self.orbs) do
    if orb.active and orb.spot == index then
      return true
    end
  end
  return false
end

-- puts a parked orb on a free spot at least 3 metres from the player
function Manager:spawn_orb()
  local free
  for _, orb in ipairs(self.orbs) do
    if not orb.active then
      free = orb
      break
    end
  end
  if free == nil then
    return
  end
  local player = self.player:get("Transform").position
  local start = math.random(#SPOTS)
  for step = 0, #SPOTS - 1 do
    local index = (start + step - 1) % #SPOTS + 1
    local spot = SPOTS[index]
    local dx, dz = spot[1] - player.x, spot[2] - player.z
    if not self:spot_taken(index) and dx * dx + dz * dz > 9 then
      free.active = true
      free.spot = index
      free.entity:get("Transform").position = vec3(spot[1], ORB_Y, spot[2])
      return
    end
  end
end

function Manager:orb_for(entity)
  for _, orb in ipairs(self.orbs) do
    if orb.entity ~= nil and entity ~= nil and orb.entity.name == entity.name then
      return orb
    end
  end
  return nil
end

-- from the orb's trigger, and from the distance check in update_playing: whichever comes first wins, an orb counts once
function Manager:collect(entity)
  if self.state ~= "playing" then
    return
  end
  local orb = self:orb_for(entity)
  if orb == nil or not orb.active then
    return
  end
  orb.active = false
  orb.entity:get("Transform").position = vec3(0, PARKED_Y, 0)

  local points = config.ORB_POINTS * self.combo
  self.score = self.score + points
  self.combo = math.min(self.combo + 1, config.COMBO_MAX)
  self.combo_timer = config.COMBO_WINDOW

  self.tag.world_anchor = self.player
  self.tag_text.text = "+" .. points
  self.tag.visible = true
  self.tag_timer = 0.7
  debug_draw.sphere(self.player:get("Transform").position, 1.2, vec4(0.4, 1, 1, 1), 0.25)
end

-- a hazard hit: the combo is lost and the player is thrown back (the player script adds `knock_vec` to its motion for a moment)
function Manager:knock(direction)
  if self.state ~= "playing" then
    return false
  end
  self.combo = 1
  self.combo_timer = 0
  self.knock_vec = direction * 7
  self.knock_time = 0.4
  return true
end

-- ---------------------------------------------------------------- per frame

function Manager:follow_camera(dt)
  local target = self.player:get("Transform").position + vec3(0, 11, 8.5)
  local transform = self.camera:get("Transform")
  transform.position = transform.position:lerp(target, 1 - math.exp(-6 * dt))
end

function Manager:update_playing(dt)
  self.time_left = self.time_left - dt
  if self.combo > 1 then
    self.combo_timer = self.combo_timer - dt
    if self.combo_timer <= 0 then
      self.combo = 1
    end
  end
  if self.knock_time > 0 then
    self.knock_time = self.knock_time - dt
  end

  -- orbs the trigger did not catch (and the way to keep the floor stocked)
  local player = self.player:get("Transform").position
  local active = 0
  for _, orb in ipairs(self.orbs) do
    if orb.active then
      active = active + 1
      local at = orb.entity:get("Transform").position
      local dx, dz = at.x - player.x, at.z - player.z
      if dx * dx + dz * dz < PICKUP_RADIUS * PICKUP_RADIUS then
        self:collect(orb.entity)
      end
    end
  end
  if active < config.ORBS_ACTIVE then
    self.spawn_timer = self.spawn_timer + dt
    if self.spawn_timer >= 0.8 then
      self.spawn_timer = 0
      self:spawn_orb()
    end
  end

  if self.tag_timer > 0 then
    self.tag_timer = self.tag_timer - dt
    if self.tag_timer <= 0 then
      self.tag.visible = false
      self.tag:clear_world_anchor()
    end
  end

  if self.time_left <= 0 then
    self.time_left = 0
    self:update_hud()
    self:end_round()
    return
  end
  if self.pushed and input.action("Gameplay", "Pause"):pressed() then
    self:pause()
  end
  self:update_hud()
end

function Manager:update_hud()
  self.score_text.text = "Score " .. self.score
  self.combo_text.text = "x" .. self.combo
  self.time_text.text = tostring(math.ceil(self.time_left))
  self.time_bar.value = self.time_left / config.ROUND_SECONDS
end

function Manager:on_update(dt)
  if not self.ready then
    if not self:panels_ready() then
      return
    end
    self:setup()
  end

  -- the Gameplay map stays on the stack for the whole arena (it is popped in on_destroy); the state decides what the actions do
  self:ensure_map()
  self.settings:update()
  self:follow_camera(dt)

  if self.state == "playing" then
    self:update_playing(dt)
  elseif self.state == "paused" and self.pushed then
    -- Pause again, Escape or the pad's B close the menu (raw controls: they are not rebindable)
    if input.action("Gameplay", "Pause"):pressed() or input.pressed("Key.Escape") or input.pressed("Pad.B") then
      self:resume()
    end
  end
end

return Manager
