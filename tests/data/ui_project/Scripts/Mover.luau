-- Moves one metre a second of game time (it stops while the game is paused).
local Mover = {}
function Mover:on_update(dt)
    local p = self.entity:position()
    self.entity:set_position(vector.create(p.x + dt, p.y, p.z))
end
return Mover
