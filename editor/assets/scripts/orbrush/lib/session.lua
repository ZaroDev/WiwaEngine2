-- Orb Rush: what the scripts share while the game runs. A required module runs once, so every script gets this same table.
local config = require("assets/scripts/orbrush/lib/config.lua")

return {
  best = 0,                 -- best score of this session
  fov = config.FOV_DEFAULT, -- field of view chosen in Settings (this session only)
  manager = nil,            -- the game manager of the arena that runs, set by its on_init
}
