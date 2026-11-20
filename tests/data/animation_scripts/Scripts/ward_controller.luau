-- Ward on his controller (Ward.nooranim, Milestone 8C part 3): a script speeds him up from standing to a run over
-- four seconds, triggers a nod at frame 90, waves from frame 120, and when the nod has ended leans into the run.
-- WardController.cs does the same in C#; the built-game check compares where every bone ends up.
-- (Counted in frames: Lua sums seconds in doubles and C# in floats, so "a second" can land a frame apart.)
local Ward = {}

function Ward:on_start()
    self.frames = 0
end

function Ward:on_update(dt)
    self.frames += 1
    self.entity:set_animation_parameter("speed", math.min(self.frames * 0.0167, 4))
    if self.frames == 90 then self.entity:set_animation_trigger("nod") end
    if self.frames == 120 then self.entity:set_animation_parameter("waving", true) end
end

function Ward:on_animation_end(name)
    if name == "Nod" then self.entity:set_animation_parameter("lean", 0.6) end
end

return Ward
