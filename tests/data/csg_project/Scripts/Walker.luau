-- Walks straight ahead (-Z) at params.speed metres per second: the CSG test checks that one walker
-- goes through the doorway and the other is stopped by the wall.
local Walker = {}

function Walker:on_update(dt)
    self.entity:character_move(vector.create(0, 0, -self.params.speed))
end

return Walker
