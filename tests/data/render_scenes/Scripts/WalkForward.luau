-- Walks its entity straight ahead (-Z) at params.speed metres per second (Milestone 8A, part 7: the camera that walks
-- into a weather zone in Scenes/Weather_Walk.noorscene; tests/weather_walk_check.cmake checks the blend).
local Walk = {}

function Walk:on_update(dt)
    self.entity:translate(vector.create(0, 0, -self.params.speed * dt))
end

return Walk
