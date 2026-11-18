-- Drags the environment's time of day from params.from to params.to (hours) over params.seconds
-- (Milestone 8A, part 2): the sun moves every frame, so the live sky and its lighting are rebuilt every
-- frame. tests/day_cycle_check.cmake measures what that costs.
local DayCycle = {}

function DayCycle:on_update(dt)
    local env = self.entity:get("environment")
    local t = math.min(noor.time() / self.params.seconds, 1)
    env.time_of_day = self.params.from + (self.params.to - self.params.from) * t
    self.entity:set("environment", env)
end

return DayCycle
