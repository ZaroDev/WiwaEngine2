-- Walks the entity's NavAgent between two points of the navmesh, for ever: Nav.agent(...):set_destination, and on_nav_arrived sends it to the other point.
-- The agent has to try again until the navmesh of the scene has loaded (set_destination is false before there is a world).
local NavPatrol = Script:extend{
  properties = {
    a = { type = "vec3", default = { 0, 0, 0 } },
    b = { type = "vec3", default = { 0, 0, 5 } },
  },
}

function NavPatrol:on_init()
  self.agent = Nav.agent(self.entity)
  self.to_b = true
  self.started = false
end

function NavPatrol:target()
  local p = self.to_b and self.b or self.a
  return vec3(p.x, p.y, p.z)
end

function NavPatrol:on_update(dt)
  if not self.started and self.agent:set_destination(self:target()) then
    self.started = true
  end
end

function NavPatrol:on_nav_arrived()
  self.to_b = not self.to_b
  self.agent:set_destination(self:target())
end

function NavPatrol:on_nav_failed()
  log_warning("NavPatrol", "no route to the next point")
end

return NavPatrol
