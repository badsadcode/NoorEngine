-- Ward walks for a second, then waves (a one-shot clip, at double speed); when the wave has ended he nods, and when
-- the nod has ended he walks on (Milestone 8C, part 2). WardWave.cs does the same in C#; the built-game check runs
-- both, and compares where every bone is at the end (which depends on the moment each end event arrived).
local Ward = {}

-- Counted in frames, not summed seconds: Lua sums in doubles and C# in floats, so "a second" can land a frame apart.
function Ward:on_start()
    self.frames = 0
end

function Ward:on_update(dt)
    if self.waved then return end
    self.frames += 1
    if self.frames >= 60 then
        self.waved = true
        self.entity:set("animator", { animation = "Walk", loop = false })  -- one-shots from here on
        self.entity:play_animation("Wave", 2)
    end
end

function Ward:on_animation_end(name)
    if name == "Wave" then
        self.entity:play_animation("Nod")
    elseif name == "Nod" then
        self.entity:set("animator", { animation = "Nod", loop = true })
        self.entity:play_animation("Walk")
    end
end

return Ward
