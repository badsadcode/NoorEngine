-- Alden Wayfarer wanders about Hillside (Milestone 8C, part 8: the 8C sample).
-- The entity is a character controller; Alden is its child "Alden", whose animator runs his controller with root
-- motion: his walk and run move him (and stop at walls, and follow the slope), this script only steers and sets the
-- pace. He faces his next waypoint (turning at most `turn` degrees a second) and eases his speed towards each leg's.
-- params: points ([[x, z], ...], a loop), speeds (m/s per leg; 0: rest there for `rest` seconds, then on at the next
-- leg's speed), turn (degrees a second), accel (m/s a second)
-- Counted in frames, not summed seconds, so a built game takes every decision on the same frame (ANIMATION.md).
local Alden = {}

function Alden:on_start()
    self.model = self.entity:child("Alden")
    self.leg, self.speed, self.facing, self.resting = 1, 0, self.params.start_facing or 0, 0
end

local function turn_towards(from, to, step)
    local d = (to - from + 540) % 360 - 180  -- the shorter way round
    return from + math.clamp(d, -step, step)
end

function Alden:on_update(dt)
    local p = self.params
    local points = p.points
    local target = points[self.leg]
    local at = self.entity:world_position()
    local dx, dz = target[1] - at.x, target[2] - at.z
    local want = p.speeds[self.leg] or 1.4
    if dx * dx + dz * dz < 0.8 * 0.8 then  -- arrived: on to the next point (or a rest first)
        if want == 0 and self.resting == 0 then
            self.resting = math.floor((p.rest or 2) * 60 + 0.5)  -- frames (the game steps 60 a second)
        end
        if self.resting > 0 then
            self.resting -= 1
            if self.resting == 0 then self.leg = self.leg % #points + 1 end
        else
            self.leg = self.leg % #points + 1
        end
        target = points[self.leg]
        dx, dz = target[1] - at.x, target[2] - at.z
    end
    if self.resting > 0 then
        want = 0
    elseif want == 0 then
        want = p.speeds[self.leg % #points + 1] or 1.4
    end
    -- Ease the pace, turn towards the point.
    local step = (p.accel or 2.5) / 60
    self.speed += math.clamp(want - self.speed, -step, step)
    if self.speed > 0.05 then
        self.facing = turn_towards(self.facing, math.deg(math.atan2(dx, dz)), (p.turn or 150) / 60)
    end
    if self.model then
        self.model:set_euler(vector.create(0, self.facing, 0))  -- Alden faces +Z
        self.model:set_animation_parameter("speed", self.speed)
    end
end

return Alden
