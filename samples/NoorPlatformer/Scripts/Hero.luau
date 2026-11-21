-- NoorPlatformer: the whole game, on the hero. The built-in `platformer` component runs and jumps (A/D or the arrows,
-- Space); this script keeps score, knocks the hero back off spikes, opens the door and draws the HUD. Until a key is
-- pressed an autopilot plays (the sample runs by itself, and tests can check the level can be finished).
local Hero = {}

-- The autopilot's jumps: where to take off (metres along the level), always running right.
local JUMPS = { 6.9, 9.6, 16.8, 18.8, 20.8, 23.0, 32.6, 38.4, 44.4, 53.4 }
local JUMP_SPEED = 11.4  -- sqrt(2 * 9.81 * 3 * 2.2): the platformer's full 2.2 m jump

function Hero:on_start()
    self.coins = 0
    self.total = 8
    self.hits = 0
    self.time = 0
    self.won = false
    self.player = false  -- someone pressed a key: the autopilot stops for good
    self.next_jump = 1
end

function Hero:autopilot()
    local e = self.entity
    local x = e:world_position().x
    e:character_move(vector.create(6.5, 0, 0))
    -- Knocked back past a jump already taken (spikes): take it again.
    while self.next_jump > 1 and JUMPS[self.next_jump - 1] > x + 0.5 do self.next_jump -= 1 end
    local at = JUMPS[self.next_jump]
    if at and x >= at then
        if e:is_grounded() then  -- in the air at the take-off point: jump on landing, if still close
            e:character_jump(JUMP_SPEED)
            self.next_jump += 1
        elseif x > at + 1.5 then
            self.next_jump += 1
        end
    elseif e:is_grounded() and e:is_on_wall() then
        e:character_jump(JUMP_SPEED)  -- something in the way: hop
    end
end

function Hero:on_update(dt)
    if noor.input.axis("move_x") ~= 0 or noor.input.down("jump") then self.player = true end
    if not self.won then
        self.time += dt
        if not self.player then self:autopilot() end
    end
    -- The HUD: 720 units tall whatever the resolution (here 4 units a game pixel), inside the game's picture.
    noor.ui.text("Coins " .. self.coins .. " / " .. self.total, 24, 16, { size = 44 })
    noor.ui.text(string.format("%.1f s", self.time), -24, 16, { size = 44, anchor = "top_right" })
    if self.won then
        noor.ui.rect(0, 0, 760, 180, { color = "#000000B0", anchor = "center" })
        noor.ui.text("You made it!", 0, -30, { size = 64, anchor = "center", color = "#FFC857" })
        noor.ui.text(self.coins .. " coins, " .. self.hits .. " ouch", 0, 40, { size = 44, anchor = "center" })
    elseif self.time < 4 and not self.player then
        noor.ui.text("A / D or arrows to run, Space to jump", 0, -24, { size = 36, anchor = "bottom" })
    end
end

function Hero:on_trigger_enter(other)
    local name = other:name()
    if string.sub(name, 1, 4) == "Coin" then
        other:destroy()
        self.coins += 1
    elseif string.sub(name, 1, 6) == "Spikes" then
        self.hits += 1
        self.entity:set_velocity(vector.create(-5, 10, 0))  -- ouch: knocked up and back
    elseif name == "Door" then
        self.won = true
    end
end

return Hero
