-- The flow of the main menu scene: title -> hero select -> the level, with the Options screen over either. The UI is two panels: "Menu"
-- (Title, Heroes, Loading) and "Options" (its own canvas, lib/options.lua). Choosing a hero stores it in prefs ("hero") and loads the level.
local Game = require("assets/game/scripts/lib/game.lua")
local Menus = require("assets/game/scripts/lib/menus.lua")
local Options = require("assets/game/scripts/lib/options.lua")

local MenuState = Script:extend{
  properties = {
    level = { type = "string", default = "assets/game/scenes/level.wscene" },
  },
}

local SCREENS = { "Title", "Heroes" }
local ROWS = { "Title/Box/Play", "Title/Box/Options", "Title/Box/Quit", "Heroes/Box/Back" }

function MenuState:on_init()
  self.panel = UI.panel(find_entity("Menu"))
  self.options_panel = UI.panel(find_entity("Options"))
  self.ready = false
  self.load = nil
  Game.ui_missing = 0
  Game.menu_state = self
  Game.set_state("title")
  pcall(input.set_cursor, "normal")
end

function MenuState:setup()
  local panel = self.panel
  local widget = function(path) return Menus.widget(panel, path) end
  local me = self

  widget("Title/Box/Play"):on("click", function() Game.set_state("heroes") end)
  widget("Title/Box/Options"):on("click", function() Options.open() end)
  widget("Title/Box/Quit"):on("click", function() quit() end)
  widget("Heroes/Box/Cards/WarriorCard/Select"):on("click", function() me:play("warrior") end)
  widget("Heroes/Box/Cards/WizardCard/Select"):on("click", function() me:play("wizard") end)
  widget("Heroes/Box/Back"):on("click", function() Game.set_state("title") end)
  widget("Title/Version").text = "Version " .. project.version
  Menus.wire_accents(panel, ROWS)

  self.loading = widget("Loading")
  self.loading_bar = widget("Loading/LoadBar")
  Options.setup(self.options_panel)
  self.ready = true
end

-- A hero was chosen: remember it and load the level (it replaces this scene when it is complete).
function MenuState:play(hero)
  if self.load ~= nil then
    return
  end
  Game.select_hero(hero)
  prefs.set("hero", hero)
  log("Menu", "playing as " .. hero)
  self.load = load_scene(self.level, "replace")
  self.loading.visible = true
end

function MenuState:on_update(dt)
  if not self.ready then
    if self.panel == nil or not self.panel.ready or self.options_panel == nil or not self.options_panel.ready then
      return
    end
    self:setup()
  end

  if self.load ~= nil then
    self.loading_bar.value = self.load:progress()
    if self.load:failed() then
      self.load = nil
      self.loading.visible = false
      log_error("Menu", "the level failed to load")
    end
  end

  -- Escape goes one step back
  if Menus.cancel_pressed() and self.load == nil then
    if Game.state == "options" then
      Options.close()
    elseif Game.state == "heroes" then
      Game.set_state("title")
    end
  end

  local base = (Game.state == "options") and Game.options_from or Game.state
  Menus.show_only(self.panel, SCREENS, base == "heroes" and "Heroes" or "Title")
  Options.update()
end

function MenuState:on_destroy()
  Options.reset()
  Game.reset()
end

return MenuState
