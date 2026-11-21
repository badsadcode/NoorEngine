-- A top-down character you steer yourself: "motion": "script" on its character means NoorEngine adds no gravity and
-- no jumps; this script sets the velocity each physics step, and the character slides along water, trees and rocks
-- (the tilemap's solid tiles). The sprite's "facing" picks its view (down, left, up, right).
local Player = {}

function Player:on_start()
    self.walking = false
end

function Player:on_fixed_update(dt)
    local dir = vector.create(noor.input.axis("move_x"), noor.input.axis("move_y"), 0)
    if vector.magnitude(dir) > 1 then dir = vector.normalize(dir) end
    self.entity:set_velocity(dir * self.params.speed)

    local moving = vector.magnitude(dir) > 0.01
    if moving then
        local sprite = self.entity:get("sprite")
        sprite.facing = { dir.x, dir.y }
        self.entity:set("sprite", sprite)
    end
    if moving ~= self.walking then
        self.entity:play_animation(moving and "walk" or "idle")
        self.walking = moving
    end
end

function Player:on_update(dt)
    if noor.time() < 5 then
        noor.ui.text("W A S D or arrows to walk", 0, -24, { size = 36, anchor = "bottom" })
    end
end

return Player
