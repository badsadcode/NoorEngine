-- Floats its entity on the water (Milestone 8D, part 2): every frame it sits at the water's height where it is, tilted
-- with the surface (noor.water_height and noor.water_normal: the same waves the renderer draws). params.sink: how far
-- below the surface its origin rides (metres).
local Float = {}

function Float:on_update(dt)
    local p = self.entity:position()
    local h = noor.water_height(p.x, p.z)
    if h == nil then return end
    local n = noor.water_normal(p.x, p.z)
    self.entity:set_position(vector.create(p.x, h - (self.params.sink or 0), p.z))
    self.entity:set_euler(vector.create(math.deg(math.atan2(n.z, n.y)), 0, -math.deg(math.atan2(n.x, n.y))))
end

return Float
