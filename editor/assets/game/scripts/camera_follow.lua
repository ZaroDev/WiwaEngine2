-- The follow camera of the level: a third-person spring arm that orbits the hero with Look and pulls in when a wall is in the way.
local Game = require("assets/game/scripts/lib/game.lua")

local CameraFollow = Script:extend{
  properties = {
    distance = { type = "float", default = 5.5, min = 1, max = 20 },
    height = { type = "float", default = 1.6, min = 0, max = 5 },
    sensitivity = { type = "float", default = 1.0, min = 0, max = 10 },
  },
}

function CameraFollow:on_init()
  self.yaw = 0
  self.pitch = -18
  self.pushed = false
end

function CameraFollow:on_update(dt)
  if not self.pushed then
    self.pushed = pcall(input.push_map, "Gameplay")
  end

  if Game.state == "playing" and self.pushed then
    local look = input.action("Gameplay", "Look"):vector()
    local sens = self.sensitivity * Game.opt.sensitivity
    local up = Game.opt.invert_y and -1 or 1
    self.yaw = self.yaw - look.x * sens
    self.pitch = math.max(-60, math.min(35, self.pitch - look.y * sens * up))
  end

  local target = Game.player_position()
  if target == nil then
    return
  end
  local distance = Game.opt.camera_distance or self.distance
  local camera = self.entity:get("Camera")
  if camera.FovY ~= Game.opt.fov then
    camera.FovY = Game.opt.fov
  end
  local rotation = quat.from_euler(self.pitch, self.yaw, 0)
  local pivot = vec3(target.x, target.y + self.height, target.z)
  local back = rotation * vec3(0, 0, 1)

  local reach = distance
  for _, hit in ipairs(physics.raycast_all(pivot, back, distance + 0.3)) do
    local name = hit.entity.name
    if name ~= "Player" and Game.by_name[name] == nil then
      reach = math.max(0.8, hit.distance - 0.3)
      break
    end
  end

  self.transform.position = pivot + back * reach
  self.transform.rotation = rotation
end

return CameraFollow
