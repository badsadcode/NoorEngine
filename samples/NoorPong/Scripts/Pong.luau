-- NoorPong (Milestone 9A, part 1): bat and ball, first to whatever you like. One script on the "Game" entity runs it all.
-- Left bat: W / S. Right bat: Up / Down. The computer plays a side until someone presses its keys.
-- Everything moves in on_fixed_update (60 Hz), so a game plays the same every time from the same seed: the editor, the
-- player and a built game agree to the bit (tests/noorpong_check.cmake).

local Pong = {}

local WALL = 8.75   -- the ball's centre stays this far from the middle, up and down (walls' inner faces minus its half size)
local BAT_X = 16    -- the bats' centres
local BAT_HALF = 1.5
local BALL_HALF = 0.25
local FACE = BAT_X - 0.25 - BALL_HALF   -- where the ball's centre touches a bat's face
local OUT = 18.5    -- past this, the point is over

-- A small deterministic random number generator (Park-Miller: exact in a Luau number), 0..1.
local function next_random(self)
    self.rng = (self.rng * 48271) % 2147483647
    return self.rng / 2147483647
end

-- The computer's aim for its next hit: off-centre to angle the ball, and less sure the faster it comes.
local function choose_aim(self, side)
    local speed = vector.magnitude(self.velocity)
    self.aim[side] = (next_random(self) * 2 - 1) * (0.8 + speed * self.params.computer_error)
end

local function serve(self, towards)
    self.ball = vector.create(0, 0, 0.1)
    local angle = (next_random(self) * 2 - 1) * math.rad(30)
    self.velocity = vector.create(towards * math.cos(angle), math.sin(angle), 0) * self.params.serve_speed
    self.wait = 0.75   -- seconds before the ball moves
    self.rally = 0
    choose_aim(self, towards < 0 and 1 or 2)   -- a fresh aim for whoever receives (not the last fast rally's)
end

function Pong:on_start()
    self.left = noor.find("Left Bat")
    self.right = noor.find("Right Bat")
    self.ball_entity = noor.find("Ball")
    self.score = { 0, 0 }
    self.human = { false, false }
    self.aim = { 0, 0 }        -- where on its face the computer means to meet the ball this time
    self.rng = math.max(1, self.params.seed or 1)
    serve(self, (next_random(self) < 0.5) and -1 or 1)
end

-- Where the ball will cross x, bouncing off the walls on the way.
local function predict(self, x)
    local t = (x - self.ball.x) / self.velocity.x
    if t < 0 then return 0 end
    local y = self.ball.y + self.velocity.y * t
    local span = 4 * WALL
    y = (y + WALL) % span
    if y < 0 then y = y + span end
    if y > 2 * WALL then y = span - y end
    return y - WALL
end

local function move_bat(self, bat, side, axis_name, dt)
    local p = bat:position()
    local axis = noor.input.axis(axis_name)
    if axis ~= 0 then self.human[side] = true end
    local y = p.y
    if self.human[side] then
        y = y + axis * self.params.bat_speed * dt
    else
        -- The computer: heads for where the ball will arrive when it's coming its way, back to the middle when it isn't.
        local coming = (side == 1 and self.velocity.x < 0) or (side == 2 and self.velocity.x > 0)
        local target = coming and (predict(self, side == 1 and -FACE or FACE) + self.aim[side]) or 0
        local step = self.params.computer_speed * dt
        y = y + math.clamp(target - y, -step, step)
    end
    y = math.clamp(y, -WALL - BALL_HALF + BAT_HALF, WALL + BALL_HALF - BAT_HALF)
    bat:set_position(vector.create(p.x, y, p.z))
    return y
end


function Pong:on_fixed_update(dt)
    local left_y = move_bat(self, self.left, 1, "left_bat", dt)
    local right_y = move_bat(self, self.right, 2, "right_bat", dt)
    if self.wait > 0 then
        self.wait = self.wait - dt
        self.ball_entity:set_position(self.ball)
        return
    end
    local b = self.ball + self.velocity * dt
    local v = self.velocity
    if b.y > WALL then b = vector.create(b.x, 2 * WALL - b.y, b.z); v = vector.create(v.x, -v.y, 0) end
    if b.y < -WALL then b = vector.create(b.x, -2 * WALL - b.y, b.z); v = vector.create(v.x, -v.y, 0) end
    -- A bat: the ball crossed its face this step, within its height. The further from the middle it hits, the steeper it goes.
    for side = 1, 2 do
        local face = side == 1 and -FACE or FACE
        local bat_y = side == 1 and left_y or right_y
        local towards = side == 1 and v.x < 0 or v.x > 0
        local crossed = side == 1 and (self.ball.x >= face and b.x < face) or (self.ball.x <= face and b.x > face)
        if towards and crossed and math.abs(b.y - bat_y) <= BAT_HALF + BALL_HALF then
            local offset = math.clamp((b.y - bat_y) / (BAT_HALF + BALL_HALF), -1, 1)
            local speed = math.min(vector.magnitude(v) * self.params.speed_up, self.params.max_speed)
            local angle = offset * math.rad(55)
            local dir = side == 1 and 1 or -1
            v = vector.create(dir * math.cos(angle), math.sin(angle), 0) * speed
            b = vector.create(2 * face - b.x, b.y, b.z)
            self.rally = self.rally + 1
            choose_aim(self, 3 - side)
        end
    end
    self.ball, self.velocity = b, v
    if b.x < -OUT then
        self.score[2] = self.score[2] + 1
        serve(self, -1)
    elseif b.x > OUT then
        self.score[1] = self.score[1] + 1
        serve(self, 1)
    end
    self.ball_entity:set_position(self.ball)
end

function Pong:on_update(dt)
    noor.ui.text(self.score[1] .. "      " .. self.score[2], 0, 28, { size = 80, anchor = "top", color = "#F2EEFF" })
    local who = (self.human[1] and "you" or "computer") .. "  ·  " .. (self.human[2] and "you" or "computer")
    noor.ui.text("W / S        " .. who .. "        Up / Down", 0, -44, { size = 22, anchor = "bottom", color = "#8E86B8" })
end

return Pong
