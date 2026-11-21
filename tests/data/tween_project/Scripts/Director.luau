-- The door rises, then grows with a bounce, then turns red: each tween started by the one before's on_done. The
-- platform goes back and forth forever (yoyo). CsDirector.cs does the same in C#.
local Director = {}

function Director:on_start()
    self.door = noor.find("Door")
    self.platform = noor.find("Platform")
    self.steps = 0
    self.door:tween("position", vector.create(0, 3, 0), 0.5, "ease_out", { on_done = function()
        self.steps += 1
        self.door:tween("scale", vector.create(1.5, 1.5, 1.5), 1.0, "bounce_out", { on_done = function()
            self.steps += 1
            self.door:tween("primitive.color", "#FF0000", 0.5, "linear", { on_done = function() self.steps += 1 end })
        end })
    end })
    self.platform:tween("position", vector.create(3, -1, 0), 1.0, "ease_in_out", { loops = -1, yoyo = true })
end

function Director:on_update()
    local d = self.door:position()
    noor.ui.text(string.format("steps %d, door y %.3f, scale %.3f, colour %s, platform x %.3f", self.steps, d.y,
        self.door:scale().x, self.door:get("primitive").color, self.platform:position().x), 20, 20)
end

return Director
