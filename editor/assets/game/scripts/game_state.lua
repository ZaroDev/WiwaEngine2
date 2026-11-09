-- The flow of the level: playing, paused, dead, won, with the Options screen over the pause menu. The HUD and the pause, dead and won
-- screens are one UI panel ("Hud", assets/game/ui/game_hud.wuipanel); Options is its own canvas (lib/options.lua). Gameplay scripts
-- only report to lib/game.lua. The hero comes from the main menu scene through prefs ("hero").
local Game = require("assets/game/scripts/lib/game.lua")
local Menus = require("assets/game/scripts/lib/menus.lua")
local Options = require("assets/game/scripts/lib/options.lua")

local GameState = Script:extend{
  properties = {
    fall_y = { type = "float", default = -6, min = -100, max = 0 },
    menu = { type = "string", default = "assets/game/scenes/main_menu.wscene" },
  },
}

local SCREENS = { "Pause", "Dead", "Won" }
local STATE_SCREEN = { paused = "Pause", dead = "Dead", won = "Won" }
local ROWS = {
  "Pause/Box/Resume", "Pause/Box/Options", "Pause/Box/Restart", "Pause/Box/MainMenu",
  "Dead/Box/Respawn", "Dead/Box/Restart", "Dead/Box/MainMenu",
  "Won/Box/Restart", "Won/Box/MainMenu",
}

function GameState:on_init()
  self.panel = UI.panel(find_entity("Hud"))
  self.options_panel = UI.panel(find_entity("Options"))
  self.ready = false
  self.pushed = false
  self.cursor = nil
  Game.ui_missing = 0
  local spawn = find_entity("Spawn point")
  if spawn ~= nil then
    local p = spawn:get("Transform").position
    Game.spawn = vec3(p.x, p.y, p.z)
    Game.checkpoint = Game.spawn
  end
  Game.player = find_entity("Player")
  Game.select_hero(prefs.get("hero", "warrior"))
  Game.set_state("playing")
end

function GameState:to_menu()
  load_scene(self.menu, "replace")
end

function GameState:setup()
  local panel = self.panel
  local widget = function(path) return Menus.widget(panel, path) end
  local me = self

  widget("Pause/Box/Resume"):on("click", function() Game.set_state("playing") end)
  widget("Pause/Box/Options"):on("click", function() Options.open() end)
  widget("Pause/Box/Restart"):on("click", function() Game.start() end)
  widget("Pause/Box/MainMenu"):on("click", function() me:to_menu() end)
  widget("Dead/Box/Respawn"):on("click", function() Game.respawn() end)
  widget("Dead/Box/Restart"):on("click", function() Game.start() end)
  widget("Dead/Box/MainMenu"):on("click", function() me:to_menu() end)
  widget("Won/Box/Restart"):on("click", function() Game.start() end)
  widget("Won/Box/MainMenu"):on("click", function() me:to_menu() end)
  Menus.wire_accents(panel, ROWS)

  self.w = {
    hero = widget("Hud/HeroName"),
    hp = widget("Hud/Hp"),
    hp_text = widget("Hud/HpText"),
    resource = widget("Hud/Resource"),
    resource_text = widget("Hud/ResourceText"),
    objective = widget("Objective"),
    boss = widget("Boss"),
    boss_bar = widget("Boss/BossBar"),
  }
  Options.setup(self.options_panel)
  self.ready = true
end

function GameState:update_hud()
  local w = self.w
  local hero = Game.hero_def()
  w.hero.text = hero.name
  w.hp.value = Game.hp / Game.max_hp
  w.hp_text.text = string.format("Health  %d / %d", math.ceil(Game.hp), Game.max_hp)
  w.resource.value = Game.resource / hero.max
  w.resource.color = Game.resource_flash > 0 and vec4(0.77, 0.29, 0.18, 1) or vec4(hero.color[1], hero.color[2], hero.color[3], hero.color[4])
  w.resource_text.text = string.format("%s  %d / %d", hero.resource, math.floor(Game.resource), hero.max)
  w.objective.text = Game.opt.hints and Game.objective() or ""
  if Game.boss ~= nil then
    w.boss.visible = true
    w.boss_bar.value = Game.boss.hp / Game.boss.max
  else
    w.boss.visible = false
  end
end

function GameState:on_update(dt)
  Game.step_teleports()

  if not self.ready then
    if self.panel == nil or not self.panel.ready or self.options_panel == nil or not self.options_panel.ready then
      return
    end
    self:setup()
  end
  if not self.pushed then
    self.pushed = pcall(input.push_map, "Gameplay")
  end

  -- Pause: the Pause action, or Escape through the UI map's Cancel (not while a rebind listens: its Escape cancels that)
  if self.pushed and Options.listening == nil then
    local cancel = Menus.cancel_pressed()
    if Game.state == "options" then
      if cancel then
        Options.close()
      end
    elseif Game.state == "playing" or Game.state == "paused" then
      if input.action("Gameplay", "Pause"):pressed() or cancel then
        Game.set_state(Game.state == "playing" and "paused" or "playing")
      end
    end
  end

  -- the fall
  if Game.state == "playing" then
    local p = Game.player_position()
    if p ~= nil and p.y < self.fall_y and not Game.teleporting(Game.player) then
      log("Game", "fell off the world")
      Game.damage_player(25)
      if Game.state == "playing" then
        Game.respawn(true) -- back to the checkpoint, without the free heal
      end
    end
  end

  local base = (Game.state == "options") and Game.options_from or Game.state
  Menus.show_only(self.panel, SCREENS, STATE_SCREEN[base])
  local cursor = Game.state == "playing" and "locked" or "normal"
  if cursor ~= self.cursor then
    self.cursor = cursor
    pcall(input.set_cursor, cursor)
  end
  self:update_hud()
  Options.update()
end

-- Stop: the shared table outlives a play session, the cursor must not stay captured.
function GameState:on_destroy()
  pcall(input.set_cursor, "normal")
  if input.listening() then
    input.cancel_listen()
  end
  Options.reset()
  Game.reset()
end

return GameState
