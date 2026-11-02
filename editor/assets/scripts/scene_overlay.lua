-- A scene-level script: no entity, listed in the scene settings.
local SceneOverlay = Script:extend{
  properties = {},
}

function SceneOverlay:on_init()
  log("Demo", "The demo scene is running")
end

function SceneOverlay:on_update(dt)
  debug_draw.box(vec3(-2.5, 0, -2.5), vec3(2.5, 3.6, 2.5), vec4(1, 0.8, 0.2, 1), 0)
  debug_draw.sphere(vec3(0, 5, 0), 0.3, vec4(1, 1, 1, 1), 0)
end

function SceneOverlay:on_destroy()
  log("Demo", "The demo scene stopped")
end

return SceneOverlay
