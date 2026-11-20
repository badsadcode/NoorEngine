-- Coth (The Uncounted) walks round the campfire (Milestone 8B, part 8: the 8B sample). The entity is a character
-- controller, so he follows the hillside; his sprite is the child "Coth sprite", an 8-direction sheet that shows the
-- view for the camera's angle. He faces the way he walks: his forward (-Z) along his velocity.
-- params: centre_x, centre_z (the fire), radius (m), speed (m/s)
local Coth = {}

function Coth:on_start()
    self.sprite = self.entity:child("Coth sprite")
    if self.sprite then self.sprite:play_animation("walk") end
end

function Coth:on_update(dt)
    local p = self.params
    local at = self.entity:world_position()
    local dx, dz = at.x - p.centre_x, at.z - p.centre_z
    local r = math.max(0.01, math.sqrt(dx * dx + dz * dz))
    -- Round the circle (anticlockwise from above), with a pull back to its radius.
    local pull = (p.radius - r) * 0.8
    local vx = -dz / r * p.speed + dx / r * pull
    local vz = dx / r * p.speed + dz / r * pull
    self.entity:character_move(vector.create(vx, 0, vz))
    self.entity:set_euler(vector.create(0, math.deg(math.atan2(-vx, -vz)), 0))
end

return Coth
