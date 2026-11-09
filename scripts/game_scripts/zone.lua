-- A trigger volume that says which part of the level the warrior is in (the objective text follows it).
local Game = require("assets/game/scripts/lib/game.lua")

local Zone = Script:extend{
  properties = {
    zone = { type = "string", default = "courtyard" },
  },
}

function Zone:on_trigger_enter(other)
  if other.name == "Player" then
    Game.zone = self.zone
  end
end

return Zone
