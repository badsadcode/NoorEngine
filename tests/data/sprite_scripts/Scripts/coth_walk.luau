-- Coth (The Uncounted) walks a circle for two seconds, attacks, and when the attack has ended, falls down
-- (Milestone 8B, part 6). CothWalk.cs does the same in C#; the built-game test checks both got there.
-- params: centre_x, centre_z (the circle's centre), radius (m), lap (seconds a lap)
local Coth = {}

function Coth:on_start()
    self.t = 0
    self.entity:play_animation("walk")
end

function Coth:on_update(dt)
    if self.attacking then return end
    self.t += dt
    local r = self.params.radius or 2
    local a = self.t / (self.params.lap or 8) * 2 * math.pi
    local cx, cz = self.params.centre_x or 0, self.params.centre_z or 0
    self.entity:set_position(vector.create(cx + r * math.sin(a), 0, cz + r * math.cos(a)))
    -- Facing along the way round: forward (-Z) along (cos a, 0, -sin a), which is a yaw of a - 90 degrees.
    self.entity:set_euler(vector.create(0, math.deg(a) - 90, 0))
    if self.t >= 2 then
        self.attacking = true
        self.entity:play_animation("attack")
    end
end

function Coth:on_animation_end(name)
    if name == "attack" then
        self.entity:play_animation("die")
    elseif name == "die" then
        self.entity:set("story", { done = true })
    end
end

return Coth
