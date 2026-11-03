-- Physics demo: a ball that hops every few seconds (body control), shows the ray it casts downwards (queries) and flashes where it hits something hard (collision events).
local Hopper = Script:extend{
  properties = {
    interval = { type = "float", default = 3, min = 0.5, max = 20 },
    speed = { type = "float", default = 6, min = 0, max = 30 },
  },
}

function Hopper:on_init()
  self.t = 0
end

function Hopper:on_physics_update(dt)
  self.t = self.t + dt
  if self.t >= self.interval then
    self.t = 0
    self.entity:body():set_velocity(vec3(0, self.speed, 0))
  end

  local from = self.transform.position
  local hit = physics.raycast(from, vec3(0, -1, 0), 20)
  if hit then
    debug_draw.line(from, hit.point, vec4(1, 1, 0, 1), 0.05)
  end
end

function Hopper:on_collision_enter(other, contact)
  if contact.impulse > 30 then
    debug_draw.sphere(contact.point, 0.25, vec4(1, 0.5, 0, 1), 0.4)
  end
end

return Hopper
