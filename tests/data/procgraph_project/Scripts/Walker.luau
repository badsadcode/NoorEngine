-- Walks in a straight line (params.x, params.z: a direction) at params.speed metres per second.
local Walker = {}

function Walker:on_update(dt)
    local p = self.params
    self.entity:character_move(vector.create(p.x * p.speed, 0, p.z * p.speed))
end

return Walker
