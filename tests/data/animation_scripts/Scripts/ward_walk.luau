-- Ward walks on root motion inside a character controller (Milestone 8C, part 4). He hears his footsteps (animation
-- events "left" and "right"), walks half as fast again after four of them, and a wall ahead stops him. WardWalk.cs
-- does the same in C#; the built-game check compares where he ends up and every bone.
local Ward = {}

function Ward:on_start()
    self.steps = 0
end

function Ward:on_animation_event(name)
    self.steps += 1
    if self.steps == 4 then self.entity:set_animation_speed(1.5) end
    self.entity:set("steps", { count = self.steps, last = name })
end

return Ward
