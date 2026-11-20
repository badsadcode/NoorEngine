"""NoorPong: the smallest real game we have (Milestone 9A, part 1: the yardsticks).

    python tools/samples/make_noorpong.py      (needs Pillow)

Writes samples/NoorPong: a two-player bat-and-ball game on an orthographic camera, in Lua, with a pixel-art ball sprite.
It's one of the yardsticks Milestone 9 is measured against: a 2D game that must stay small, fast and simple however big
NoorEngine's worlds get (M9_PLAN.md, principle 0). Everything here is plain files, written as the editor would write
them; the scene has no terrain, no world grid, nothing procedural.

  NoorPong.noorproj, .gitignore, README.md
  Settings/input.json                 left bat W/S, right bat Up/Down
  Assets/Sprites/ball.png             an 8 x 8 pixel ball, drawn here pixel by pixel (ours, reproducible)
  Assets/Sprites/ball.noorsprite      16 pixels a metre: half a metre across, crisp
  Scripts/Pong.luau                   the game (the computer plays a side until someone presses its keys)
  Scenes/Pong.noorscene               the table, walls, bats, ball, the 2D camera and a sun
"""
import hashlib
import json
import pathlib
import uuid

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
PROJECT = ROOT / "samples" / "NoorPong"
NS = uuid.UUID("9a01b0e0-0000-4000-8000-00000000f00d")


def gid(name):
    return str(uuid.uuid5(NS, name))


def write(rel, text):
    p = PROJECT / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text, encoding="utf-8", newline="\n")
    return p


def meta(path, guid, importer, settings):
    sha = hashlib.sha256(path.read_bytes()).hexdigest()
    m = {"guid": guid, "importer": importer, "noor_meta": 1, "settings": settings, "source_sha256": sha}
    path.with_name(path.name + ".noormeta").write_text(json.dumps(m, indent=2) + "\n", newline="\n")


BALL = [
    "..XXXX..",
    ".XWWWWX.",
    "XWWWWWWX",
    "XWWWWWWX",
    "XWWWWWWX",
    "XWWWWWGX",
    ".XWWWGX.",
    "..XXXX..",
]


def ball_png():
    img = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
    colours = {"X": (196, 186, 255, 255), "W": (255, 255, 255, 255), "G": (210, 204, 236, 255)}
    for y, row in enumerate(BALL):
        for x, c in enumerate(row):
            if c in colours:
                img.putpixel((x, y), colours[c])
    p = PROJECT / "Assets" / "Sprites" / "ball.png"
    p.parent.mkdir(parents=True, exist_ok=True)
    img.save(p)
    meta(p, gid("ball.png"), "texture", {"kind": "color", "max_size": 4096, "mips": False, "normal_convention": "opengl"})
    sheet = {
        "noor_sprite": 1,
        "name": "Ball",
        "texture": gid("ball.png"),
        "grid": {"columns": 1, "rows": 1, "margin": 0, "spacing": 0},
        "pivot": [0.5, 0.5],
        "pixels_per_metre": 16,
        "filter": "nearest",
        "animations": {},
    }
    s = write("Assets/Sprites/ball.noorsprite", json.dumps(sheet, indent=2) + "\n")
    meta(s, gid("ball.noorsprite"), "sprite", {})


def entity(name, position, components, rotation=(0, 0, 0, 1)):
    return {
        "id": gid("entity " + name),
        "name": name,
        "layer": "default",
        "tags": [],
        "parent": None,
        "transform": {"position": list(position), "rotation": list(rotation), "scale": [1, 1, 1]},
        "components": components,
    }


def box(size, colour, roughness=0.6):
    return {"primitive": {"shape": "box", "size": list(size), "color": colour, "roughness": roughness}}


def scene():
    ents = [
        # The camera looks down -Z at the table (the XY plane): 20 m of it from top to bottom, whatever the window's width.
        entity("Camera", (0, 0, 10), {"camera": {"projection": "orthographic", "height": 20, "near": 0.1, "far": 50, "ev100": 14},
                                      "audio_listener": {}}),
        # Straight onto the table, so everything facing the camera is evenly lit; no shadows to speak of.
        entity("Sun", (0, 0, 20), {"light": {"type": "directional", "color": "#FFFFFF", "illuminance": 90000, "shadows": False,
                                             "sky_color": "#B8B0FF", "sky_luminance": 3000}}),
        entity("Table", (0, 0, -0.6), box((40, 24, 0.2), "#141127", 0.9)),
        entity("Wall Top", (0, 9.25, 0), box((36, 0.5, 0.5), "#E8E4F8")),
        entity("Wall Bottom", (0, -9.25, 0), box((36, 0.5, 0.5), "#E8E4F8")),
        entity("Left Bat", (-16, 0, 0), box((0.5, 3, 0.5), "#A07CFF")),
        entity("Right Bat", (16, 0, 0), box((0.5, 3, 0.5), "#FFC857")),
        entity("Ball", (0, 0, 0.1), {"sprite": {"sheet": gid("ball.noorsprite"), "frame": 0, "billboard": "camera", "cast_shadows": False}}),
        entity("Game", (0, 0, 0), {"script": {"file": "Scripts/Pong.luau",
                                              "params": {"serve_speed": 12, "speed_up": 1.08, "max_speed": 30, "bat_speed": 14,
                                                         "computer_speed": 10, "computer_error": 0.12, "seed": 2026}}}),
    ]
    for i in range(9):  # the net: dashes down the middle
        ents.append(entity(f"Net {i + 1}", (0, -8 + i * 2, -0.4), box((0.2, 1, 0.1), "#4A4370")))
    s = {"noor_scene": 2, "id": gid("scene Pong"), "name": "Pong", "entities": ents}
    write("Scenes/Pong.noorscene", json.dumps(s, indent=2) + "\n")


PONG_LUAU = r'''-- NoorPong (Milestone 9A, part 1): bat and ball, first to whatever you like. One script on the "Game" entity runs it all.
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
'''

README = """# NoorPong

The smallest real game in NoorEngine's samples, and one of Milestone 9's **yardsticks**: a 2D game that must stay
small, fast and simple however big the engine's worlds get ([M9 plan](../../docs/milestones/M9_PLAN.md), principle 0;
the numbers are in [M9_YARDSTICKS.md](../../docs/milestones/M9_YARDSTICKS.md)).

- **Play:** left bat **W / S**, right bat **Up / Down**. The computer plays a side until someone presses its keys.
- **How it's made:** an orthographic camera (`"projection": "orthographic"`, 20 m high) looking at boxes on a table, a
  pixel-art sprite for the ball, and one Lua script, `Scripts/Pong.luau`, that moves everything at the fixed 60 Hz
  step, so a game plays the same every time from the same seed.
- **Generated** by `tools/samples/make_noorpong.py` (the ball is drawn there pixel by pixel).

No terrain, no world grid, no streaming, nothing procedural: none of that is needed to make a game.
"""


def main():
    write("NoorPong.noorproj", json.dumps({"noor_project": 1, "id": gid("project"), "name": "NoorPong", "engine": "0.0.1",
                                            "created": "2026-11-20T12:00:00Z"}, indent=2) + "\n")
    write(".gitignore", "# Derived data and editor leftovers (NoorEditor and noor_cli recreate them)\n.noor/\n*.bak\n")
    write("README.md", README)
    write("Settings/input.json", json.dumps({"axes": {"left_bat": {"negative": ["S"], "positive": ["W"]},
                                                       "right_bat": {"negative": ["Down"], "positive": ["Up"]}}}, indent=2) + "\n")
    write("Scripts/Pong.luau", PONG_LUAU)
    ball_png()
    scene()
    print("NoorPong written to", PROJECT)


if __name__ == "__main__":
    main()
