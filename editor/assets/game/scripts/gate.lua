-- The trigger in front of the gate of the boss arena. Once the approach is cleared and the warrior stands here, the arena scene loads
-- additively while a bar shows the progress; the door (door.lua, needs_arena) opens when it is done.
local Game = require("assets/game/scripts/lib/game.lua")

local Gate = Script:extend{
  properties = {
    scene = { type = "string", default = "assets/game/scenes/boss_arena.wscene" },
    group = { type = "string", default = "approach" },
  },
}

function Gate:on_init()
  self.load = nil
  self.requested = false
  self.inside = false
  self.panel = UI.panel(find_entity("Hud"))
  Game.loads = 0
  Game.on_restart(function() self.inside = false end)
end

function Gate:on_trigger_enter(other)
  if other.name == "Player" then
    self.inside = true
  end
end

function Gate:on_trigger_exit(other)
  if other.name == "Player" then
    self.inside = false
  end
end

function Gate:on_update(dt)
  if self.inside and not self.requested and Game.state == "playing" and Game.alive_in_group(self.group) == 0 then
    self.requested = true
    Game.loads = Game.loads + 1
    self.load = load_scene(self.scene, "additive")
    log("Gate", "loading " .. self.scene)
  end

  local ready = self.panel ~= nil and self.panel.ready
  local widget = ready and self.panel:find("Loading") or nil
  if self.load ~= nil then
    if widget ~= nil then
      widget.visible = true
      self.panel:find("Loading/LoadBar").value = self.load:progress()
    end
    if self.load:done() then
      self.load = nil
      Game.boss_ready = true
      if widget ~= nil then
        widget.visible = false
      end
    elseif self.load:failed() then
      self.load = nil
      self.requested = false
      log_error("Gate", "the arena failed to load; step away and back to retry")
      if widget ~= nil then
        widget.visible = false
      end
    end
  end
end

return Gate
