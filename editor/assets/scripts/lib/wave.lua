local wave = {}

-- A sine in [-1, 1] with a period of 2 * pi.
function wave.sine(t)
  return math.sin(t)
end

return wave
