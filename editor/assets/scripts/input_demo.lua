-- Input actions (assets/input/demo.winput): a ball moved with Move and Jump, Fire flashes, and the HUD shows the device, the Move vector
-- and the Jump bindings; "Rebind Jump" listens for the next key and saves it. In the editor click the Viewport image first.
local InputDemo = Script:extend{
  properties = {
    speed = { type = "float", default = 4, min = 0, max = 20 },
    jump_speed = { type = "float", default = 6, min = 0, max = 20 },
  },
}

function InputDemo:on_init()
  self.panel = UI.panel(find_entity("Hud"))
  self.pushed = false
  self.ready = false
  self.listening = false
  self.want_jump = false
end

function InputDemo:setup()
  local panel = self.panel
  self.device = panel:find("InputInfo/Device")
  self.move = panel:find("InputInfo/Move")
  self.jump = panel:find("InputInfo/JumpKey")
  self.rebind_label = panel:find("InputInfo/RebindJump/Label")
  panel:find("InputInfo/RebindJump"):on("click", function(me) Audio.post("UI_Click") me:start_rebind() end)
  self.ready = true
end

function InputDemo:start_rebind()
  if self.listening then
    return
  end
  self.listening = true
  self.rebind_label.text = "Press a key..."
  input.listen(function(control)
    input.rebind("Gameplay", "Jump", 1, control)
    input.save_bindings()
  end)
end

function InputDemo:update_hud()
  if not self.ready then
    if self.panel == nil or not self.panel.ready then
      return
    end
    self:setup()
  end

  -- Escape cancels a listen without calling back; the prompt goes back to normal either way
  if self.listening and not input.listening() then
    self.listening = false
    self.rebind_label.text = "Rebind Jump"
  end

  local move = input.action("Gameplay", "Move"):vector()
  self.device.text = project.name .. " " .. project.version .. " - Device: " .. input.last_device()
  self.move.text = string.format("Move: %.2f, %.2f", move.x, move.y)

  local keys = {}
  for _, binding in ipairs(input.bindings("Gameplay", "Jump")) do
    keys[#keys + 1] = binding.controls[1]
  end
  self.jump.text = "Jump: " .. table.concat(keys, ", ")
end

function InputDemo:on_update(dt)
  -- the Gameplay map lives in the project's input map, which loads in the background: push it as soon as it is there
  if not self.pushed then
    self.pushed = pcall(input.push_map, "Gameplay")
    if not self.pushed then
      return
    end
  end

  -- a press lasts one frame and a frame is shorter than a physics step: it is kept until the next step uses it
  if input.action("Gameplay", "Jump"):pressed() then
    self.want_jump = true
  end

  if input.action("Gameplay", "Fire"):pressed() then
    debug_draw.sphere(self.transform.position, 0.7, vec4(1, 0.3, 0.1, 1), 0.3)
  end

  self:update_hud()
end

function InputDemo:on_physics_update(dt)
  if not self.pushed then
    return
  end

  local body = self.entity:body()
  local velocity = body:get_velocity() or vec3(0, 0, 0)
  local move = input.action("Gameplay", "Move"):vector()
  body:add_impulse(vec3((move.x * self.speed - velocity.x) * 0.5, 0, (-move.y * self.speed - velocity.z) * 0.5))

  if self.want_jump then
    self.want_jump = false
    body:add_impulse(vec3(0, self.jump_speed, 0))
  end
end

return InputDemo
