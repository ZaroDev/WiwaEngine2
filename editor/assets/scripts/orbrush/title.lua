-- Orb Rush: the main menu, on the "TitleLogic" entity of the title scene. Play loads the arena (showing the progress on the button), Settings
-- opens the shared Settings panel, Quit asks the host to end the program (in the editor that stops Play).

local config = require("assets/scripts/orbrush/lib/config.lua")
local Menu = require("assets/scripts/orbrush/lib/menu.lua")

local Title = Script:extend{
  properties = {},
}

function Title:on_init()
  self.main = UI.panel(find_entity("MainMenu"))
  self.settings_panel = UI.panel(find_entity("SettingsMenu"))
  self.settings = Menu.new_settings(self.settings_panel, self)
  self.ready = false
  self.load = nil
  Menu.apply_fov()
end

function Title:setup()
  self.play_label = self.main:find("Menu/Play/Label")
  self.main:find("Menu/Play"):on("click", function(me) me:start_game() end)
  self.main:find("Menu/Settings"):on("click", function(me)
    me.main.visible = false
    me.settings_panel.visible = true
    me.settings:open()
  end)
  self.main:find("Menu/Quit"):on("click", function() quit() end)
  self.settings_panel.visible = false
  self.ready = true
end

function Title:start_game()
  if self.load ~= nil then
    return
  end
  self.load = load_scene(config.ARENA_SCENE, "replace")
end

-- called by the Settings panel's Back button
function Title:on_settings_closed()
  self.settings_panel.visible = false
  self.main.visible = true
end

function Title:on_update(dt)
  if not self.ready then
    if not (Menu.ready(self.main) and Menu.ready(self.settings_panel)) then
      return
    end
    self:setup()
  end
  self.settings:update()

  if self.load ~= nil then
    if self.load:failed() then
      self.load = nil
      self.play_label.text = "Load failed - try again"
    elseif not self.load:done() then
      self.play_label.text = string.format("Loading %d%%", math.floor(self.load:progress() * 100))
    end
  end
end

return Title
