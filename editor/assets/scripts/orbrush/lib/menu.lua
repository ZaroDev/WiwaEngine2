-- Orb Rush: helpers the menus share. The Settings menu is one panel used by the title scene and by the arena's pause menu.
local config = require("assets/scripts/orbrush/lib/config.lua")
local session = require("assets/scripts/orbrush/lib/session.lua")

local Menu = {}

local DEFAULT_NOTE = "Field of view and best score last for this session. Controls are saved."

-- a panel is an asset that loads in the background
function Menu.ready(panel)
  return panel ~= nil and panel.ready
end

-- the primary camera takes the field of view chosen in Settings
function Menu.apply_fov()
  local entity = camera.primary()
  if entity ~= nil then
    entity:get("Camera").FovY = session.fov
  end
end

-- "Key.W" -> "W"
local function short(control)
  return (control:gsub("^Key%.", ""):gsub("^Pad%.", "Pad "))
end

-- the controls of every binding of an action, for showing
local function bound_text(action)
  local lines = {}
  for _, binding in ipairs(input.bindings("Gameplay", action)) do
    local names = {}
    for _, control in ipairs(binding.controls) do
      names[#names + 1] = short(control)
    end
    lines[#lines + 1] = table.concat(names, " ")
  end
  return table.concat(lines, "  /  ")
end

local Settings = {}
Settings.__index = Settings

-- `owner` has on_settings_closed(): Back calls it. Call update() every frame, open() when the panel is shown.
function Menu.new_settings(panel, owner)
  return setmetatable({ panel = panel, owner = owner, ready = false, listening = nil, note = DEFAULT_NOTE }, Settings)
end

function Settings:setup()
  local panel = self.panel
  self.move = panel:find("Menu/MoveKey")
  self.dash = panel:find("Menu/DashKey")
  self.pause = panel:find("Menu/PauseKey")
  self.rebind_dash = panel:find("Menu/RebindDash/Label")
  self.rebind_pause = panel:find("Menu/RebindPause/Label")
  self.fov_label = panel:find("Menu/FovLabel")
  self.fov = panel:find("Menu/Fov")
  self.note_text = panel:find("Menu/Note")

  panel:find("Menu/RebindDash"):on("click", function() self:start_rebind("Dash", self.rebind_dash) end)
  panel:find("Menu/RebindPause"):on("click", function() self:start_rebind("Pause", self.rebind_pause) end)
  panel:find("Menu/Reset"):on("click", function()
    input.reset_all()
    input.save_bindings()
    self.note = DEFAULT_NOTE
    self:refresh()
  end)
  self.fov:on("value_changed", function(_, value)
    session.fov = value
    self.fov_label.text = "Field of view " .. math.floor(value)
    Menu.apply_fov()
  end)
  panel:find("Menu/Back"):on("click", function()
    if input.listening() then
      input.cancel_listen()
    end
    self.listening = nil
    self.owner:on_settings_closed()
  end)
  self.ready = true
end

-- Dash and Pause have one control each on the keyboard (binding 1); Move is a four key composite and is only shown
function Settings:start_rebind(action, label)
  if input.listening() then
    return
  end
  self.listening = { action = action, label = label }
  label.text = "Press a key..."
  input.listen(function(control)
    local ok, conflicts = input.rebind("Gameplay", action, 1, control)
    input.save_bindings()
    self.note = DEFAULT_NOTE
    if not ok and conflicts ~= nil then
      local others = {}
      for _, conflict in ipairs(conflicts) do
        if conflict.action ~= action then
          others[#others + 1] = conflict.action
        end
      end
      if #others > 0 then
        self.note = short(control) .. " is also used by " .. table.concat(others, ", ")
      end
    end
  end)
end

function Settings:refresh()
  self.move.text = "Move: " .. bound_text("Move")
  self.dash.text = "Dash: " .. bound_text("Dash")
  self.pause.text = "Pause: " .. bound_text("Pause")
  self.rebind_dash.text = "Rebind Dash"
  self.rebind_pause.text = "Rebind Pause"
  self.note_text.text = self.note
  self.fov_label.text = "Field of view " .. math.floor(session.fov)
end

function Settings:open()
  if self.ready then
    self.fov.value = session.fov
    self:refresh()
  end
end

function Settings:update()
  if not self.ready then
    if not Menu.ready(self.panel) then
      return
    end
    self:setup()
    self:open()
  end
  -- Escape ends a listen without a call: the labels go back to normal either way
  if self.listening ~= nil and not input.listening() then
    self.listening = nil
    self:refresh()
  end
end

return Menu
