-- Shared code, loaded with require (gameplay foundations, part 1).
local util = {}
util.answer = 42
function util.lane(i) return (i - 5.5) * 0.8 end  -- where the i-th bullet starts, along x
return util
