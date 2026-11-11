-- A behaviour tree condition leaf (LuaCondition): succeeds while the guard's blackboard holds a noise it has not gone to look at yet (heard_kind is set
-- by the Perceiver when it hears AI.emit_noise; the tree clears it after the visit).
return BtCondition:extend{
  on_tick = function(self, bb, dt)
    if bb:get("heard_kind") ~= "" then return "success" end
    return "failure"
  end,
}
