-- The HUD (assets/ui/demo_hud.wuipanel) on the entity that has the UICanvas: change a widget (score, health bar, beacon bar),
-- listen to one (the button switches the light, the slider sets its intensity), use a widget that follows an entity on the screen and play a video.
local DemoUi = Script:extend{
  properties = {},
}

function DemoUi:on_init()
  self.panel = UI.panel(self.entity)
  self.light = find_entity("Demo light")
  self.t = 0
  self.on = true
  self.ready = false
end

-- The panel is an asset that loads in the background: its widgets are looked up once it is ready.
function DemoUi:setup()
  local panel = self.panel
  self.score = panel:find("Stats/Score")
  self.health = panel:find("Stats/Health")
  self.beacon = panel:find("BeaconTag/BeaconBar")
  self.label = panel:find("Controls/ToggleLight/Label")
  self.slider = panel:find("Controls/LightIntensity")
  self.level = self.slider.value

  panel:find("Controls/ToggleLight"):on("click", function(me)
    Audio.post("UI_Click")
    me.on = not me.on
    me:apply_light()
  end)
  self.slider:on("value_changed", function(me, value)
    me.level = value
    me:apply_light()
  end)

  -- Video (see "Video" in docs/scripting.md): the Play intro button shows the full screen Video widget and plays it; the widget hides itself when
  -- the video ends, is skipped (the Skip button, or Cancel: Escape / pad B) or fails.
  self.intro = panel:find("Intro")
  panel:find("PlayIntro"):on("click", function(me)
    Audio.post("UI_Click")
    me.intro.visible = true
    me.intro:video_play()
  end)
  panel:find("Intro/Skip"):on("click", function(me) me.intro:video_stop() end)
  self.intro:on("finished", function(me, info)
    me.intro.visible = false
    if info.failed then
      log("Video", "the intro could not play: " .. me.intro:video_error())
    end
  end)

  self.ready = true
  self:apply_light()
end

function DemoUi:apply_light()
  if self.light ~= nil and self.light:is_valid() then
    self.light:get("Light").intensity = self.on and self.level or 0
  end
  self.label.text = self.on and "Light: on" or "Light: off"
end

function DemoUi:on_update(dt)
  if not self.ready then
    if self.panel == nil or not self.panel.ready then
      return
    end
    self:setup()
  end

  if self.intro.visible and input.action("UI", "Cancel"):pressed() then
    self.intro:video_stop()
  end

  self.t = self.t + dt
  self.score.text = "Score " .. math.floor(self.t * 10)
  self.health.value = 0.5 + 0.5 * math.sin(self.t)
  self.beacon.value = (self.t * 0.2) % 1
end

return DemoUi
