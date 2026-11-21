-- NoorIsometric: walking in an isometric room. W A S D or the arrows walk on screen (up is up), only onto floor cells;
-- the sprite's "facing" picks which of Coth's eight views shows. Until a key is pressed an autopilot walks a loop
-- round the pillars (passing behind two of them).
local Walker = {}

local SPEED = 3.0
local CW, CH = 2.0, 1.0  -- the map's cell: a 2 x 1 m diamond
local WALKABLE = { [0] = true, [1] = true, [2] = true }  -- floor tiles; walls (3, 4, 5) aren't
-- The autopilot's loop, in cells (x, y): round the four pillars.
local LOOP = { {2, 2}, {9, 2}, {9, 9}, {5.5, 9}, {5.5, 5.5}, {2, 5.5} }

-- The cell under a point (the inverse of where the map puts cells: x - y across, x + y up).
local function cell_at(px, py)
    local fx = px / CW + py / CH
    local fy = py / CH - px / CW
    return math.floor(fx + 0.5), math.floor(fy + 0.5)
end

local function cell_centre(x, y)
    return vector.create((x - y) * CW * 0.5, (x + y) * CH * 0.5, 0)
end

function Walker:on_start()
    self.map = noor.find(self.params.map or "Room")
    self.player = false
    self.target = 2
    self.walking = false
end

function Walker:walkable(p)
    if not self.map then return true end
    local x, y = cell_at(p.x, p.y)
    local t = self.map:tile(x, y)
    return t ~= nil and WALKABLE[t] == true
end

function Walker:on_update(dt)
    local e = self.entity
    local dir = vector.create(noor.input.axis("move_x"), noor.input.axis("move_y"), 0)
    if dir.x ~= 0 or dir.y ~= 0 then self.player = true end
    if not self.player then
        local goal = LOOP[self.target]
        local to = cell_centre(goal[1], goal[2]) - e:position()
        if vector.magnitude(to) < 0.1 then
            self.target = self.target % #LOOP + 1
        else
            dir = to
        end
    end
    local moving = vector.magnitude(dir) > 0.01
    if moving then
        dir = vector.normalize(dir)
        local here = e:position()
        local step = dir * SPEED * dt
        -- Slide along walls: try the whole step, then each axis on its own.
        for _, s in ipairs({ step, vector.create(step.x, 0, 0), vector.create(0, step.y, 0) }) do
            if self:walkable(here + s) then
                e:set_position(here + s)
                break
            end
        end
        local sprite = e:get("sprite")
        sprite.facing = { dir.x, dir.y }
        e:set("sprite", sprite)
    end
    if moving ~= self.walking then
        e:play_animation(moving and "walk" or "idle")
        self.walking = moving
    end
end

return Walker
