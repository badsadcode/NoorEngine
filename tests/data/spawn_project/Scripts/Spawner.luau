-- Spawns ten bullets a tenth of a second apart, tags every other one "even", teleports the first back up at 1.5 s, and
-- writes what it saw on the HUD. CsSpawner.cs does the same in C#; the test compares them.
local util = require("util")
local Spawner = {}

function Spawner:on_start()
    self.spawned = {}
    self.teleported = false
end

function Spawner:on_update(dt)
    local t = noor.time()
    while #self.spawned < 10 and t >= #self.spawned * 0.1 do
        local i = #self.spawned + 1
        local b = noor.spawn("Assets/Bullet.noorprefab", vector.create(util.lane(i), 3 + i * 0.1, 0))
        if i % 2 == 0 then b:add_tag("even") end
        table.insert(self.spawned, b)
    end
    if t >= 1.5 and not self.teleported then
        self.teleported = self.spawned[1]:teleport(vector.create(0, 5, 0))
    end
    local trail = self.spawned[1] and self.spawned[1]:child("Trail")
    noor.ui.text(string.format("spawned %d, bullets %d, even %d, trail %s, teleported %s",
        #self.spawned, #noor.find_all("bullet"), #noor.find_all("even"), tostring(trail ~= nil), tostring(self.teleported)), 20, 20)
    if self.spawned[1] then
        local p = self.spawned[1]:world_position()
        noor.ui.text(string.format("first %.2f %.2f", p.x, p.y), 20, 60)
    end
    noor.ui.text("util " .. util.answer, 20, 100)
end

return Spawner
