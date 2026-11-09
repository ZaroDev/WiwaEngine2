-- A plank of the bridge: hears on_joint_break when its hinge goes over its break force, and the plank falls with the rest.
local Game = require("assets/game/scripts/lib/game.lua")

local BridgePlank = Script:extend{}

function BridgePlank:on_joint_break(other)
  Game.broken_joints = Game.broken_joints + 1
  log("Bridge", self.name .. " broke away")
  debug_draw.sphere(self.transform.position, 0.8, vec4(1, 0.4, 0.1, 1), 1.0)
end

return BridgePlank
