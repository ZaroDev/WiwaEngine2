-- A shared helper: scripts load it with require. Edit it while the game plays and every script that uses it changes.
local wave = {}

function wave.sine(t)
  return math.sin(t)
end

return wave
