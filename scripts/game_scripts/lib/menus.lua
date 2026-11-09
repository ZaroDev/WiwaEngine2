-- Helpers the menus of both scenes share (the main menu scene and the level): widget lookup, the brass accent bar of the text rows, and
-- which root screen of a panel is shown.
local Game = require("assets/game/scripts/lib/game.lua")

local Menus = {}

-- The widget at a path. A stand-in (counted in Game.ui_missing and logged) if there is none, so one typo does not stop a whole menu.
function Menus.widget(panel, path)
  local found = panel:find(path)
  if found == nil then
    Game.ui_missing = Game.ui_missing + 1
    log_warning("Menus", "no widget " .. path)
    return { on = function() end, find = function() return nil end }
  end
  return found
end

-- The brass bar of a text row (its "Accent" child) shows while the pointer or the focus is on it, and only on one row of a list at a time.
function Menus.wire_accents(panel, paths)
  local accents = {}
  for _, path in ipairs(paths) do
    accents[path] = Menus.widget(panel, path .. "/Accent")
  end
  local function only(path)
    for other, accent in pairs(accents) do
      accent.visible = (other == path)
    end
  end
  for _, path in ipairs(paths) do
    local button = Menus.widget(panel, path)
    button:on("hover_enter", function() only(path) end)
    button:on("hover_exit", function() accents[path].visible = false end)
    button:on("focus", function() only(path) end)
  end
end

-- Shows the root screen `shown` of the panel (a name from `names`, or nil for none) and hides the others.
function Menus.show_only(panel, names, shown)
  for _, name in ipairs(names) do
    panel:find(name).visible = (name == shown)
  end
end

-- true when Escape (or B on a pad) was pressed this frame, through the UI map
function Menus.cancel_pressed()
  local pressed = false
  pcall(function() pressed = input.action("UI", "Cancel"):pressed() end)
  return pressed
end

return Menus
