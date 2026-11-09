-- The Options screen (its own UI panel, on its own canvas "Options", used by the main menu scene and by the level's pause menu).
-- Every option is applied at once and stored: graphics through settings.graphics (render_quality.json) and settings.scene, the rest in
-- prefs (user_prefs.json, written when the screen is closed). The scene that owns the canvas sets Options.panel and calls setup/update.
local Game = require("assets/game/scripts/lib/game.lua")
local Menus = require("assets/game/scripts/lib/menus.lua")

local Options = {}

local AA_LABELS = { [0] = "Off", [1] = "FXAA", [2] = "TAA" }
local REBINDABLE = { "Jump", "Fire", "Melee", "Pause" }
local TAB_NAMES = { graphics = "Graphics", controls = "Controls", game = "Game" }
local BRASS = { 0.85, 0.66, 0.34, 1 }
local BONE = { 0.93, 0.89, 0.81, 1 }

local function widget(path)
  return Menus.widget(Options.panel, path)
end

local function pref(key, default)
  return prefs.get("opt_" .. key, default)
end

-- The current choices: the engine's graphics settings first, then what the player stored.
function Options.load()
  local g = settings.graphics.get()
  local look = settings.scene.get()
  local o = Game.opt
  o.fov = pref("fov", o.fov)
  o.sensitivity = pref("sensitivity", o.sensitivity)
  o.invert_y = pref("invert_y", o.invert_y)
  o.difficulty = pref("difficulty", o.difficulty)
  o.camera_distance = pref("camera_distance", o.camera_distance)
  o.hints = pref("hints", o.hints)
  Options.gfx = {
    vsync = pref("vsync", g.vsync),
    shadow = pref("shadow", g.shadow_distance),
    bloom = pref("bloom", look.bloom),
    ao = pref("ao", look.ao),
    motion_blur = pref("motion_blur", look.motion_blur),
    aa = pref("aa", g.taa and 2 or (g.fxaa and 1 or 0)),
  }
  Options.apply_graphics()
end

function Options.apply_graphics()
  local gfx = Options.gfx
  settings.graphics.set{ vsync = gfx.vsync, shadow_distance = gfx.shadow, fxaa = gfx.aa == 1, taa = gfx.aa == 2 }
  settings.scene.set{ bloom = gfx.bloom, ao = gfx.ao, motion_blur = gfx.motion_blur }
end

function Options.set_graphic(key, value)
  Options.gfx[key] = value
  prefs.set("opt_" .. key, value)
  Options.apply_graphics()
  Options.refresh()
end

function Options.set_option(key, value)
  Game.opt[key] = value
  prefs.set("opt_" .. key, value)
  Options.refresh()
end

function Options.key_of(action)
  local first = input.bindings("Gameplay", action)[1]
  return first ~= nil and first.controls[1] or "-"
end

-- Puts the stored values into the widgets.
function Options.refresh()
  local w = Options.w
  if w == nil then
    return
  end
  local gfx = Options.gfx
  w.vsync.value = gfx.vsync and 1 or 0
  w.shadows.value = gfx.shadow
  w.bloom.value = gfx.bloom and 1 or 0
  w.ao.value = gfx.ao and 1 or 0
  w.motion_blur.value = gfx.motion_blur and 1 or 0
  w.aa_label.text = AA_LABELS[gfx.aa]
  w.fov.value = Game.opt.fov
  w.sensitivity.value = Game.opt.sensitivity
  w.invert_y.value = Game.opt.invert_y and 1 or 0
  w.difficulty_label.text = Game.diff().name
  w.camera_distance.value = Game.opt.camera_distance
  w.hints.value = Game.opt.hints and 1 or 0
  for _, action in ipairs(REBINDABLE) do
    w.keys[action].text = (Options.listening == action) and "Press a key..." or Options.key_of(action)
  end
end

function Options.start_rebind(action)
  if Options.listening ~= nil then
    return
  end
  Options.listening = action
  Options.refresh()
  input.listen(function(control)
    local ok, others = input.rebind("Gameplay", action, 1, control)
    if ok then
      Options.w.note.text = ""
    else
      local other = others[1]
      Options.w.note.text = control .. " is also used by " .. tostring(other and other.action or "another action")
    end
    input.save_bindings()
  end)
end

function Options.show_tab(tab)
  Options.w.tabs.graphics.visible = (tab == "graphics")
  Options.w.tabs.controls.visible = (tab == "controls")
  Options.w.tabs.game.visible = (tab == "game")
  for key, label in pairs(Options.w.tab_labels) do -- the active tab is brass
    local c = (key == tab) and BRASS or BONE
    label.color = vec4(c[1], c[2], c[3], c[4])
  end
end

-- Opens the screen over whatever state the game is in; close() goes back to it.
function Options.open()
  if Game.state == "options" then
    return
  end
  Game.options_from = Game.state
  Options.show_tab("graphics")
  Options.refresh()
  Options.panel.visible = true
  Game.set_state("options")
end

-- Closes the screen; the options are written to user_prefs.json unless `keep_file` says not to (the self test).
function Options.close(keep_file)
  if Options.listening ~= nil then
    input.cancel_listen()
  end
  if not keep_file then
    prefs.save()
  end
  Options.panel.visible = false
  Game.set_state(Game.options_from or "title")
end

function Options.setup(panel)
  Options.panel = panel
  local w = { keys = {}, tabs = {} }
  Options.w = w

  w.tabs.graphics = widget("Options/Box/GraphicsTab")
  w.tabs.controls = widget("Options/Box/ControlsTab")
  w.tabs.game = widget("Options/Box/GameTab")
  w.tab_labels = {}
  for key, name in pairs(TAB_NAMES) do
    w.tab_labels[key] = widget("Options/Box/Tabs/" .. name .. "/Label")
  end
  widget("Options/Box/Tabs/Graphics"):on("click", function() Options.show_tab("graphics") end)
  widget("Options/Box/Tabs/Controls"):on("click", function() Options.show_tab("controls") end)
  widget("Options/Box/Tabs/Game"):on("click", function() Options.show_tab("game") end)
  widget("Options/Box/Back"):on("click", function() Options.close() end)
  Menus.wire_accents(panel, { "Options/Box/Back" })

  -- graphics
  w.vsync = widget("Options/Box/GraphicsTab/Vsync/Control")
  w.shadows = widget("Options/Box/GraphicsTab/Shadows/Control")
  w.bloom = widget("Options/Box/GraphicsTab/Bloom/Control")
  w.ao = widget("Options/Box/GraphicsTab/Ao/Control")
  w.motion_blur = widget("Options/Box/GraphicsTab/MotionBlur/Control")
  w.aa = widget("Options/Box/GraphicsTab/AntiAliasing/Control")
  w.aa_label = widget("Options/Box/GraphicsTab/AntiAliasing/Control/Label")
  w.fov = widget("Options/Box/GraphicsTab/Fov/Control")
  w.vsync:on("value_changed", function(me, v) Options.set_graphic("vsync", v > 0.5) end)
  w.shadows:on("value_changed", function(me, v) Options.set_graphic("shadow", v) end)
  w.bloom:on("value_changed", function(me, v) Options.set_graphic("bloom", v > 0.5) end)
  w.ao:on("value_changed", function(me, v) Options.set_graphic("ao", v > 0.5) end)
  w.motion_blur:on("value_changed", function(me, v) Options.set_graphic("motion_blur", v > 0.5) end)
  w.aa:on("click", function() Options.set_graphic("aa", (Options.gfx.aa + 1) % 3) end)
  w.fov:on("value_changed", function(me, v) Options.set_option("fov", v) end)

  -- controls
  for _, action in ipairs(REBINDABLE) do
    local button = widget("Options/Box/ControlsTab/" .. action .. "/Control")
    w.keys[action] = widget("Options/Box/ControlsTab/" .. action .. "/Control/Label")
    button:on("click", function() Options.start_rebind(action) end)
  end
  w.sensitivity = widget("Options/Box/ControlsTab/Sensitivity/Control")
  w.invert_y = widget("Options/Box/ControlsTab/InvertY/Control")
  w.note = widget("Options/Box/ControlsTab/Note")
  w.sensitivity:on("value_changed", function(me, v) Options.set_option("sensitivity", v) end)
  w.invert_y:on("value_changed", function(me, v) Options.set_option("invert_y", v > 0.5) end)
  widget("Options/Box/ControlsTab/Reset/Control"):on("click", function()
    input.reset_all()
    input.save_bindings()
    w.note.text = "Controls reset"
    Options.refresh()
  end)

  -- game
  w.difficulty = widget("Options/Box/GameTab/Difficulty/Control")
  w.difficulty_label = widget("Options/Box/GameTab/Difficulty/Control/Label")
  w.camera_distance = widget("Options/Box/GameTab/CameraDistance/Control")
  w.hints = widget("Options/Box/GameTab/Hints/Control")
  w.difficulty:on("click", function() Options.set_option("difficulty", Game.opt.difficulty % #Game.difficulties + 1) end)
  w.camera_distance:on("value_changed", function(me, v) Options.set_option("camera_distance", v) end)
  w.hints:on("value_changed", function(me, v) Options.set_option("hints", v > 0.5) end)

  local ok, err = pcall(Options.load)
  if not ok then
    log_warning("Options", "the options could not be read: " .. tostring(err))
  end
  Options.refresh()
  Options.show_tab("graphics")
  panel.visible = false
  Options.ready = true
end

-- every frame while the screen is open
function Options.update()
  if Options.listening ~= nil and not input.listening() then
    Options.listening = nil
    Options.refresh()
  end
  if Options.listening ~= nil and Game.state ~= "options" then
    input.cancel_listen()
  end
end

-- the scene is going away
function Options.reset()
  Options.ready = false
  Options.w = nil
  Options.listening = nil
end

return Options
