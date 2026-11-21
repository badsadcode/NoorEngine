-- Plays the door's "open" timeline half a second in, at 1.5x speed; the timeline calls "creak" and the hinge says
-- "latched"; when it ends, the door hears "timeline_end". CsDoor.cs does the same in C#.
local Door = {}

function Door:on_start()
    self.log = {}
    noor.after(0.5, function() self.entity:play_timeline("open", 1.5) end)
    self.entity:child("Hinge"):on("latched", function(data) table.insert(self.log, "latched " .. data.how) end)
    self.entity:on("timeline_end", function(data) table.insert(self.log, "end " .. data.name) end)
end

function Door:creak(n) table.insert(self.log, "creak x" .. n) end

function Door:on_update()
    noor.ui.text(string.format("playing %s, hinge %.2f, light %.1f, %s", tostring(self.entity:timeline()),
        self.entity:child("Hinge"):euler().y, self.entity:get("light").intensity, table.concat(self.log, ", ")), 20, 20)
end

return Door
