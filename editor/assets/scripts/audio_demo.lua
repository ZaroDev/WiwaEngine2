-- Audio (see "Audio" in docs/scripting.md): the entity has an AudioEmitter, so it has a voice: it hums the ambience (PlayOnStart, heard across the hall) and
-- this script blips from the beacon every few seconds. The sounds are assets/audio/*.ogg and .wav (CC0, see its LICENSE.txt); the project plays them
-- with the MiniAudio backend (Settings, Audio). Another backend, or none (Null), takes the same calls.
local AudioDemo = Script:extend{
  properties = {
    event = { type = "string", default = "Beacon_Blip" },
    every = { type = "float", default = 4, min = 0.5, max = 60 },
  },
}

function AudioDemo:on_init()
  self.t = 0
  Audio.set_rtpc("Volume", 0.8)
  self.last = Audio.post(self.event, self.entity)
end

function AudioDemo:on_update(dt)
  self.t = self.t + dt
  if self.t >= self.every then
    self.t = 0
    self.last = Audio.post(self.event, self.entity)
  end
end

return AudioDemo
