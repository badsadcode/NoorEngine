-- Shooter (Lua twin of Shooter.cs): the game itself. Click to shoot along the crosshair; hit the
-- gold targets before the round ends. Lives on the Camera (so its forward is where you look).
--
-- Parts of the game ("state", shared with FpsPlayer through this entity's "game" component):
--   demo    - before the first click: the game plays itself; click to play
--   playing - a round is on; Esc frees the mouse and pauses
--   paused  - click to carry on
--   over    - time's up: the score; click to play again

local SHOT_SOUND = "Assets/Audio/shot.wav"
local HIT_SOUND = "Assets/Audio/hit.wav"
local END_SOUND = "Assets/Audio/round_end.wav"
local TARGET_COLOR = "#F2C14E"
local FLASH_COLOR = "#FFFFFF"

-- Where targets appear after a hit (x, height, z), in front of the starting spot.
local SPOTS = {
    vector.create(-8, 1.8, -8), vector.create(7, 1.5, -10), vector.create(0, 2.2, -16),
    vector.create(12, 1.2, -2), vector.create(-14, 2.0, -2), vector.create(4, 1.4, -4),
    vector.create(-4, 2.6, -18), vector.create(15, 2.4, -15), vector.create(-16, 1.3, -12),
    vector.create(9, 2.8, 9), vector.create(-10, 1.6, 14), vector.create(16, 1.8, 6),
    vector.create(-3, 1.2, 2), vector.create(1, 3.0, -9),
}

local Shooter = {}

function Shooter:on_start()
    math.randomseed(7)
    self.state = "demo"
    self.time_left = 0
    self.cooldown = 0
    self.hit_marker = 0
    self.hits = 0
    self.shots = 0
    self.was_captured = false
    self.player = self.entity:parent()
    self.hit_targets = {} -- target -> seconds left flashing white, then moved
    self.targets = {}
    local i = 1
    while noor.find("Target " .. i) do
        table.insert(self.targets, noor.find("Target " .. i))
        i += 1
    end
    self:publish()
end

function Shooter:on_update(dt)
    self.cooldown -= dt
    self.hit_marker -= dt
    local click = noor.input.pressed("fire")

    if self.state == "demo" then
        if click then
            self:start_round()
        elseif self.cooldown <= 0 and self:target_under_crosshair() then
            self:fire()
        end
    elseif self.state == "playing" then
        if noor.input.mouse_captured() then
            self.was_captured = true
        end
        if self.was_captured and not noor.input.mouse_captured() then
            self.state = "paused" -- Esc gave the mouse back
        else
            if click and self.cooldown <= 0 then
                self:fire()
            end
            self.time_left -= dt
            if self.time_left <= 0 then
                self.time_left = 0
                self.state = "over"
                noor.play_sound(END_SOUND, nil, 0.8)
            end
        end
    elseif self.state == "paused" then
        if click then
            self:resume()
        end
    elseif self.state == "over" then
        if click then
            self:start_round()
        end
    end

    self:update_hit_targets(dt)
    self:publish()
    self:draw_hud()
end

function Shooter:start_round()
    self.hits = 0
    self.shots = 0
    self.time_left = self.params.round_seconds or 60
    self.cooldown = 0.2 -- the click that starts the round doesn't shoot
    self:resume()
end

function Shooter:resume()
    self.state = "playing"
    self.was_captured = false
    noor.input.capture_mouse(true)
end

-- Entities can't be table keys across calls (each lookup is a new handle), so hit targets are
-- remembered by id.
function Shooter:is_live_target(e)
    return e:get("target") ~= nil and self.hit_targets[e:id()] == nil
end

function Shooter:aim()
    return noor.raycast(self.entity:world_position(), self.entity:forward(), 200, self.player)
end

function Shooter:target_under_crosshair()
    local hit = self:aim()
    return hit ~= nil and self:is_live_target(hit.entity)
end

local function set_color(e, color)
    local primitive = e:get("primitive")
    if primitive then
        primitive.color = color
        e:set("primitive", primitive)
    end
end

function Shooter:fire()
    self.shots += 1
    self.cooldown = if self.state == "demo" then 0.6 else 0.15
    noor.play_sound(SHOT_SOUND, nil, 0.6)
    local hit = self:aim()
    if hit == nil then
        return
    end
    if self:is_live_target(hit.entity) then
        self.hits += 1
        self.hit_marker = 0.15
        noor.play_sound(HIT_SOUND, hit.point)
        set_color(hit.entity, FLASH_COLOR)
        self.hit_targets[hit.entity:id()] = { entity = hit.entity, left = 0.15 }
    else
        local body = hit.entity:get("rigidbody")
        if body and body.type == "dynamic" then
            hit.entity:add_impulse(self.entity:forward() * 40) -- crates get knocked about
        end
    end
end

-- A hit target flashes white for a moment, then turns up somewhere else.
function Shooter:update_hit_targets(dt)
    for id, info in self.hit_targets do
        info.left -= dt
        if info.left <= 0 then
            self.hit_targets[id] = nil
            info.entity:set_position(self:free_spot())
            set_color(info.entity, TARGET_COLOR)
        end
    end
end

function Shooter:free_spot()
    local player = if self.player then self.player:world_position() else vector.create(0, 0, 0)
    for _ = 1, 20 do
        local spot = SPOTS[math.random(#SPOTS)]
        local flat = vector.create(spot.x - player.x, 0, spot.z - player.z)
        local taken = false
        for _, t in self.targets do
            if vector.magnitude(t:world_position() - spot) < 1.5 then
                taken = true
            end
        end
        if vector.magnitude(flat) >= 6 and not taken then -- not in the player's face, not taken
            return spot
        end
    end
    return SPOTS[math.random(#SPOTS)]
end

function Shooter:publish()
    self.entity:set("game", { state = self.state })
end

-- ---- HUD (UI units: the screen is 720 tall) ----

local function panel(height)
    noor.ui.rect(0, 0, 760, height, { color = "#100C1CC0", anchor = "center" })
end

function Shooter:draw_hud()
    local accuracy = if self.shots == 0 then 0 else math.round(100 * self.hits / self.shots)
    if self.state == "playing" or self.state == "demo" then
        if self.hit_marker > 0 then
            noor.ui.rect(0, 0, 10, 10, { color = "#FF5050", anchor = "center" })
        else
            noor.ui.rect(0, 0, 4, 4, { color = "#FFFFFFC0", anchor = "center" })
        end
    end

    if self.state == "demo" then
        noor.ui.text("DEMO · Hits: " .. self.hits, 24, 20, { size = 22, color = "#FFFFFFA0" })
        -- Low on the screen, so the demo's shooting stays in view.
        noor.ui.rect(0, -30, 760, 190, { color = "#100C1CC0", anchor = "bottom" })
        noor.ui.text("NoorFPS-lite", 0, -140, { size = 56, color = "#F2C14E", anchor = "bottom" })
        if math.floor(noor.time() * 2) % 2 == 0 then
            noor.ui.text("Click to play", 0, -90, { size = 28, anchor = "bottom" })
        end
        noor.ui.text("WASD walk · Shift sprint · Space jump · Click shoot · Esc frees the mouse", 0, -50,
            { size = 18, color = "#FFFFFFB0", anchor = "bottom" })
    elseif self.state == "playing" then
        noor.ui.text("Hits: " .. self.hits, 24, 20, { size = 36 })
        noor.ui.text(`Shots: {self.shots} · {accuracy}%`, 24, 62, { size = 20, color = "#FFFFFFB0" })
        local seconds = math.ceil(self.time_left)
        noor.ui.text(string.format("%d:%02d", seconds // 60, seconds % 60), 0, 20,
            { size = 40, color = if seconds <= 10 then "#FF5050" else "#FFFFFF", anchor = "top" })
    elseif self.state == "paused" then
        panel(160)
        noor.ui.text("Paused", 0, -25, { size = 56, anchor = "center" })
        noor.ui.text("Click to carry on", 0, 35, { size = 26, color = "#FFFFFFC0", anchor = "center" })
    elseif self.state == "over" then
        panel(240)
        noor.ui.text("Time!", 0, -65, { size = 64, color = "#F2C14E", anchor = "center" })
        noor.ui.text(`Hits: {self.hits} · Accuracy: {accuracy}%`, 0, 10, { size = 32, anchor = "center" })
        noor.ui.text("Click to play again", 0, 70, { size = 24, color = "#FFFFFFC0", anchor = "center" })
    end
end

return Shooter
