-- The game's own check of the main menu scene, run by `wiwa_editor --game-smoke` (which opens this scene and raises the signal: the x of the
-- entity "SmokeSignal" becomes 1). It checks the scene and its screens, chooses a hero like a player would and loads the level with the flag
-- prefs "smoke", which makes selftest.lua walk the level. Logs "GAMESMOKE FAIL: <why>" on a failure.
local Game = require("assets/game/scripts/lib/game.lua")
local Options = require("assets/game/scripts/lib/options.lua")

local MenuSelftest = Script:extend{}

local function check(condition, message)
  if not condition then
    error(message, 0)
  end
end

local function near(a, b, distance)
  return vec3(a.x - b.x, 0, a.z - b.z):length() <= distance
end

local steps = {}

local function step(name, fn)
  steps[#steps + 1] = { name = name, fn = fn }
end

step("title", function(me, dt, s)
  check(Options.ready, "the menu UI is not set up (a panel did not load?)")
  check(Game.ui_missing == 0, "the menu looks for " .. Game.ui_missing .. " widgets that its panels do not have")
  check(Game.state == "title", "the menu should start on the title screen, it is " .. Game.state)
  for _, name in ipairs({ "Stage Warrior", "Stage Wizard", "Dragon" }) do
    local figure = find_entity(name)
    check(figure ~= nil and Animation.animator(figure) ~= nil, name .. " is missing or has no animator")
    check(Animation.animator(figure).playing, name .. " does not play its animation")
  end
  return true
end)

step("options", function(me, dt, s)
  if me.t == 0 then
    Options.open()
    check(Game.state == "options" and Game.options_from == "title", "the options screen did not open over the title")
  end
  if me.t > 0.3 then
    Options.show_tab("controls")
    Options.show_tab("game")
    Options.close(true)
    check(Game.state == "title", "the options screen should go back to the title")
    return true
  end
end)

step("heroes", function(me, dt, s)
  if me.t == 0 then
    Game.set_state("heroes")
  end
  if me.t > 2.0 then
    local camera = find_entity("Camera"):get("Transform").position
    check(near(camera, Game.stage.camera, 0.5), "the camera did not glide to the hero stage")
    return true
  end
end)

step("play", function(me, dt, s)
  local state = find_entity("MenuState")
  check(state ~= nil, "the MenuState entity is missing")
  prefs.set("smoke", true)
  -- the same call the Choose Wizard button makes
  Game.menu_state:play("wizard")
  return true
end)

function MenuSelftest:on_init()
  self.signal = find_entity("SmokeSignal")
  self.index = 1
  self.t = 0
  self.finished = false
end

function MenuSelftest:on_update(dt)
  if self.finished or self.signal == nil or self.signal:get("Transform").position.x < 0.5 then
    return
  end
  local current = steps[self.index]
  if current == nil then
    self.finished = true
    return
  end
  local ok, result = pcall(current.fn, self, dt, {})
  if not ok then
    self.finished = true
    log("GameSmoke", "GAMESMOKE FAIL: " .. current.name .. ": " .. tostring(result))
    return
  end
  if result then
    log("GameSmoke", "step " .. current.name .. " ok")
    self.index = self.index + 1
    self.t = 0
  else
    self.t = self.t + dt
  end
end

return MenuSelftest
