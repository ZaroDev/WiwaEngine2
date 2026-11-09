-- A trigger volume: touching it saves the entity's own position as the place to respawn.
local Game = require("assets/game/scripts/lib/game.lua")

local Checkpoint = Script:extend{}

function Checkpoint:on_trigger_enter(other)
  if other.name == "Player" then
    local p = self.transform.position
    Game.set_checkpoint(vec3(p.x, p.y, p.z))
  end
end

return Checkpoint
