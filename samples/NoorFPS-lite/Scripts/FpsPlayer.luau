-- FpsPlayer (Lua twin of FpsPlayer.cs): walk (WASD), sprint (Shift), jump (Space), look (mouse),
-- footsteps. Before the first click the game runs its demo: the player turns towards the nearest
-- target by itself, and Shooter (on the Camera) fires when one is under the crosshair.
-- Shooter says which part of the game we're in through the Camera's "game" component.

local STEP_SOUND = "Assets/Audio/step.wav"
local DEMO_TURN_SPEED = 150 -- degrees per second

local Player = {}

local function turn_towards(current, target, max_step)
    local delta = ((target - current) % 360 + 540) % 360 - 180 -- the short way round
    if math.abs(delta) <= max_step then
        return current + delta
    end
    return current + math.sign(delta) * max_step
end

function Player:on_start()
    self.yaw = self.entity:euler().y
    self.pitch = 0
    self.step_timer = 0
    self.camera = self.entity:child("Camera")
    self.targets = {}
    local i = 1
    while noor.find("Target " .. i) do
        table.insert(self.targets, noor.find("Target " .. i))
        i += 1
    end
end

function Player:aim_at_nearest_target(dt)
    if not self.camera then
        return
    end
    local eye = self.camera:world_position()
    local nearest = nil
    for _, target in self.targets do
        local to = target:world_position() - eye
        if nearest == nil or vector.magnitude(to) < vector.magnitude(nearest) then
            nearest = to
        end
    end
    if nearest == nil then
        return
    end
    -- Forward is -Z: yaw turns it about +Y, pitch (positive looks up) about +X.
    local want_yaw = math.deg(math.atan2(-nearest.x, -nearest.z))
    local want_pitch = math.deg(math.atan2(nearest.y, math.sqrt(nearest.x * nearest.x + nearest.z * nearest.z)))
    self.yaw = turn_towards(self.yaw, want_yaw, DEMO_TURN_SPEED * dt)
    self.pitch = turn_towards(self.pitch, want_pitch, DEMO_TURN_SPEED * dt)
end

function Player:on_update(dt)
    local p = self.params
    local game = self.camera and self.camera:get("game")
    local state = if game and game.state then game.state else "demo"

    if state == "demo" then
        self:aim_at_nearest_target(dt)
    elseif noor.input.mouse_captured() then
        local look = p.look_sensitivity or 0.12
        self.yaw -= noor.input.axis("look_x") * look
        self.pitch = math.clamp(self.pitch - noor.input.axis("look_y") * look, -85, 85)
    end
    self.entity:set_euler(vector.create(0, self.yaw, 0))
    if self.camera then
        self.camera:set_euler(vector.create(self.pitch, 0, 0))
    end

    if state ~= "playing" then
        return -- no walking in the demo, while paused or on the results
    end

    local move = self.entity:forward() * noor.input.axis("move_y") + self.entity:right() * noor.input.axis("move_x")
    if vector.magnitude(move) > 1 then
        move = vector.normalize(move)
    end
    local sprinting = noor.input.down("sprint")
    local speed = (p.speed or 5) * (if sprinting then (p.sprint or 1.7) else 1)
    self.entity:character_move(move * speed)
    if noor.input.pressed("jump") then
        self.entity:character_jump(p.jump_speed or 5)
    end

    -- A footstep every so often while walking on the ground (faster when sprinting).
    if self.entity:is_grounded() and vector.magnitude(move) > 0.1 then
        self.step_timer -= dt
        if self.step_timer <= 0 then
            noor.play_sound(STEP_SOUND, self.entity:world_position(), 0.5)
            self.step_timer = if sprinting then 0.3 else 0.45
        end
    else
        self.step_timer = 0.1 -- the first step comes soon after starting to walk
    end
end

return Player
