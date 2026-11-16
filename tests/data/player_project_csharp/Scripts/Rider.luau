-- Rises at params.speed metres per second: the player test checks a script ran.
local Rider = {}

function Rider:on_update(dt)
    self.entity:translate(vector.create(0, self.params.speed * dt, 0))
end

return Rider
