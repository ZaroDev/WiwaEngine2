-- A scene-level script (listed in the scene settings, no entity): logs and draws debug shapes around the stations.
local SceneOverlay = Script:extend{
  properties = {},
}

function SceneOverlay:on_init()
  log("Demo", "The demo hall is running: keys 1 to 7 jump between the stations")
end

function SceneOverlay:on_update(dt)
  debug_draw.box(vec3(-2.5, 0, -2.5), vec3(2.5, 3.6, 2.5), vec4(1, 0.8, 0.2, 1), 0)
  debug_draw.box(vec3(5.5, 0, 0), vec3(7.5, 2, 3), vec4(0.2, 1, 0.4, 1), 0)
end

function SceneOverlay:on_destroy()
  log("Demo", "The demo hall stopped")
end

return SceneOverlay
