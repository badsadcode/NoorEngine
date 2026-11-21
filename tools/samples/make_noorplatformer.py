"""NoorPlatformer: the 2D foundations' platformer sample (2D part 7).

    python tools/samples/make_noorplatformer.py      (needs Pillow)

Writes samples/NoorPlatformer: a short pixel-art level to run and jump through, built only from public engine features:
a tilemap level, the built-in `platformer` character, coins and spikes as triggers, a door, a HUD, a pixel-perfect
320 x 180 display, a follow camera and parallax layers. One Lua script (on the hero) is the whole game.

The art is Tom's own (hand-drawn for an old game of his, used here with his permission, 2026-11-21). This script cuts
from his original files once, by a local script outside this repository; the committed PNGs are the sample's art. This
script writes everything else (the level, the scene, the script, the settings) and the art's metadata.

  NoorPlatformer.noorproj, .gitignore, README.md
  Settings/display.json           320 x 180, viewport stretch, whole steps
  Assets/Sprites/hero.png         the hero: idle, run, jump (12 x 18 pixel frames)
  Assets/Sprites/coin.png         a coin spinning (4 frames)
  Assets/Sprites/tiles.png        16 x 16 tiles: stone, brick, grass, planks, a crate, torches, spikes, a door
  Assets/Sprites/mountains.png, pine.png, sky.png   the backdrop (sky.png is one pixel, drawn here)
  Assets/Tiles/level.noortileset  which tiles are solid and which are one-way
  Scripts/Hero.luau               the game: coins, spikes, the door, the HUD, and an autopilot until a key is pressed
  Scenes/Level.noorscene          the level
"""
import hashlib
import json
import pathlib
import sys
import uuid

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
PROJECT = ROOT / "samples" / "NoorPlatformer"
NS = uuid.UUID("9a07d000-0000-4000-8000-00000000c0de")
PPM = 16  # pixels a metre: one tile is one metre


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


def texture(img, rel):
    p = PROJECT / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    if img is not None:
        img.save(p)
    meta(p, gid(rel), "texture", {"kind": "color", "max_size": 4096, "mips": False, "normal_convention": "opengl"})
    return gid(rel)


def sheet(rel, tex, columns, rows, pivot, animations=None, ppm=PPM):
    s = {"noor_sprite": 1, "name": pathlib.Path(rel).stem, "texture": tex,
         "grid": {"columns": columns, "rows": rows, "margin": 0, "spacing": 0},
         "pivot": pivot, "pixels_per_metre": ppm, "filter": "nearest", "animations": animations or {}}
    p = write(rel, json.dumps(s, indent=2) + "\n")
    meta(p, gid(rel), "sprite", {})
    return gid(rel)


# ---- The art -------------------------------------------------------------------------------------------------------

# The tiles, in the order the level uses them.
T = dict(COBBLE=0, COBBLE2=1, COBBLE3=2, DARK=3, DARK2=4, DEEP=5, RED=6, RED_BLOCK=7, DARK_BRICK=8, RED_DARK=9,
         GRASS=10, GRASS2=11, GRASS3=12, COBWEB=13, PLANK=14, CRATE=15, TORCH=16, SPIKES=17, DOOR=18)
SOLID = [T[k] for k in ("COBBLE", "COBBLE2", "COBBLE3", "DARK", "DARK2", "DEEP", "RED", "RED_BLOCK", "DARK_BRICK", "RED_DARK", "CRATE")]
ONE_WAY = [T["PLANK"]]


# The art PNGs are committed; they were cut once from Tom's original files by a local script that isn't part of this
# repository (the original game stays private). A missing PNG means a broken checkout, not something to regenerate.
if not (PROJECT / "Assets" / "Sprites" / "tiles.png").exists():
    sys.exit("samples/NoorPlatformer/Assets/Sprites/*.png are missing: they're committed art, not generated")
art = {}

tiles_sheet = sheet("Assets/Sprites/tiles.noorsprite", texture(art.get("tiles"), "Assets/Sprites/tiles.png"), 19, 1, [0.5, 0.5])
hero_sheet = sheet("Assets/Sprites/hero.noorsprite", texture(art.get("hero"), "Assets/Sprites/hero.png"), 11, 1, [0.5, 0.0], {
    "idle": {"frames": [0, 1, 2], "fps": 7, "loop": True},
    "run": {"frames": [4, 5, 6, 7, 8], "fps": 10, "loop": True},
    "jump": {"frames": [10], "fps": 5, "loop": True},
    "fall": {"frames": [10], "fps": 5, "loop": True}})
coin_sheet = sheet("Assets/Sprites/coin.noorsprite", texture(art.get("coin"), "Assets/Sprites/coin.png"), 4, 1, [0.5, 0.5], {
    "spin": {"frames": [0, 1, 2, 3], "fps": 8, "loop": True}})
mountains_sheet = sheet("Assets/Sprites/mountains.noorsprite", texture(art.get("mountains"), "Assets/Sprites/mountains.png"), 1, 1, [0.5, 0.0])
pine_sheet = sheet("Assets/Sprites/pine.noorsprite", texture(art.get("pine"), "Assets/Sprites/pine.png"), 1, 1, [0.5, 0.0])
sky_img = Image.new("RGBA", (1, 1), (24, 20, 46, 255))  # ours: one dark violet pixel, stretched
sky_sheet = sheet("Assets/Sprites/sky.noorsprite", texture(sky_img, "Assets/Sprites/sky.png"), 1, 1, [0.5, 0.5], ppm=1)

tileset = {"noor_tileset": 1, "name": "Level", "sheet": tiles_sheet, "solid": SOLID, "one_way": ONE_WAY}
p = write("Assets/Tiles/level.noortileset", json.dumps(tileset, indent=2) + "\n")
meta(p, gid("Assets/Tiles/level.noortileset"), "tileset", {})
TILESET = gid("Assets/Tiles/level.noortileset")

# ---- The level ------------------------------------------------------------------------------------------------------
# A grid of cells, x to the right, y up; (0, 0) bottom-left. The ground's top row is y = 1 (its surface at 1.5 m). A full
# jump rises 2.2 m, so every ledge is at most 2 m above where you jump from, and every pit can be climbed out of.
W, H = 64, 14
grid = {}


def put(x, y, t):
    grid[(x, y)] = t


def box(x0, x1, y0, y1, t):
    for x in range(x0, x1 + 1):
        for y in range(y0, y1 + 1):
            put(x, y, t)


for x in range(W):  # the ground: dark stone under cobbles
    put(x, 0, T["DARK2"] if x % 3 else T["DARK"])
    put(x, 1, [T["COBBLE"], T["COBBLE2"], T["COBBLE3"]][x % 3])
box(0, 0, 2, H - 1, T["DARK_BRICK"])         # walls at both ends
box(W - 1, W - 1, 2, H - 1, T["DARK_BRICK"])
put(8, 2, T["CRATE"])                         # a crate to hop onto
box(11, 14, 3, 3, T["PLANK"])                 # planks to jump up through
for i, top in enumerate([2, 3, 4]):           # a red brick staircase, a step a metre
    box(18 + 2 * i, 19 + 2 * i, 2, top, T["RED"])
box(25, 27, 2, 2, T["SPIKES"])                # spikes in the gap...
box(28, 30, 2, 3, T["RED_BLOCK"])             # ...and a block beyond it (2 m: a fall in can climb out)
box(34, 38, 3, 3, T["PLANK"])                 # high road: planks, then higher planks
box(40, 44, 5, 5, T["PLANK"])
box(46, 50, 2, 2, T["SPIKES"])                # spikes on the ground, a plank bridge over them
box(45, 51, 3, 3, T["PLANK"])
box(55, 56, 2, 2, T["RED_DARK"])              # a last step
put(60, 2, T["DOOR"])                         # the way out
for x, t in [(3, T["GRASS"]), (5, T["GRASS3"]), (15, T["GRASS2"]), (24, T["GRASS"]), (33, T["GRASS3"]), (53, T["GRASS2"]), (58, T["GRASS"])]:
    put(x, 2, t)
for x, y in [(1, 12), (W - 2, 12)]:
    put(x, y, T["COBWEB"])
for x in (1, W - 2):  # torches on the end walls
    put(x, 4, T["TORCH"])

rows = []
for y in range(H - 1, -1, -1):
    rows.append(" ".join(str(grid[(x, y)]) if (x, y) in grid else "." for x in range(W)))

ents = []


def entity(name, pos, comps, parent=None, scale=(1, 1, 1)):
    e = {"id": gid("e " + name), "name": name, "layer": "default", "tags": [], "parent": parent,
         "transform": {"position": list(pos), "rotation": [0, 0, 0, 1], "scale": list(scale)}, "components": comps}
    ents.append(e)
    return e


def sprite(sheet_id, **extra):
    s = {"sheet": sheet_id, "billboard": "none", "cast_shadows": False, "unlit": True}
    s.update(extra)
    return {"sprite": s}


START = (3.0, 1.51)
CAMERA_HEIGHT = 180 / PPM  # 11.25 m: exactly the 320 x 180 base resolution's height in 16-pixel metres
entity("Camera", (START[0] + 6, 6.125, 20), {
    "camera": {"projection": "orthographic", "height": CAMERA_HEIGHT, "near": 0.1, "far": 60, "ev100": 14, "pixel_snap": True},
    "camera_follow": {"target": "Hero", "offset": [2, 2], "dead_zone": [3, 2], "smoothing": 0.15, "bounds": [-0.5, -0.5, W - 0.5, H - 0.5]}})
entity("Sky", (0, 0, -10), dict(sprite(sky_sheet, layer=-3), parallax={"factor": [0, 0], "anchor": [0, 0]}), scale=(60, 30, 1))
far = entity("Mountains", (0, 0, -8), {"parallax": {"factor": [0.15, 0.3], "anchor": [START[0] + 6, 6.125]}})
for i in range(3):
    entity(f"Mountains {i + 1}", (-10 + i * 18.75, -2.5, 0), sprite(mountains_sheet, layer=-2), parent=far["id"])
near = entity("Pines", (0, 0, -6), {"parallax": {"factor": [0.5, 0.7], "anchor": [START[0] + 6, 6.125]}})
for i, x in enumerate(range(-4, 66, 7)):
    entity(f"Pine {i + 1}", (x, -0.5 + (i % 2) * 0.5, 0), sprite(pine_sheet, layer=-1), parent=near["id"])
entity("Level", (0, 0, 0), {"tilemap": {"tileset": TILESET, "rows": rows, "unlit": True}})

COINS = [(8, 3.5), (12.5, 4.6), (23, 5.5), (29, 4.6), (36, 4.6), (42, 6.6), (48, 4.6), (55.5, 3.6)]
for i, (x, y) in enumerate(COINS):
    entity(f"Coin {i + 1}", (x, y, 0.05), dict(sprite(coin_sheet, animation="spin", order=1),
                                               collider={"shape": "rectangle", "size": [0.6, 0.8], "trigger": True}))
for name, (x0, x1) in {"Spikes 1": (25, 27), "Spikes 2": (46, 50)}.items():
    entity(name, ((x0 + x1) / 2, 2.2, 0), {"collider": {"shape": "rectangle", "size": [x1 - x0 + 0.8, 0.5], "trigger": True}})
entity("Door", (60, 2, 0), {"collider": {"shape": "rectangle", "size": [0.8, 1], "trigger": True}})
entity("Hero", (START[0], START[1], 0.1), dict(sprite(hero_sheet, animation="idle", order=2), **{
    "character": {"radius": 0.3, "height": 1.05, "plane": "xy", "gravity_scale": 3, "acceleration": 40, "deceleration": 50,
                  "air_acceleration": 25, "coyote_time": 0.1, "jump_buffer": 0.1},
    "platformer": {"speed": 6.5, "jump_height": 2.2, "animations": {"idle": "idle", "run": "run", "jump": "jump", "fall": "fall"}},
    "script": {"file": "Scripts/Hero.luau"}}))

write("Scripts/Hero.luau", r'''-- NoorPlatformer: the whole game, on the hero. The built-in `platformer` component runs and jumps (A/D or the arrows,
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
''')

write("Settings/display.json", json.dumps({"window": {"width": 1280, "height": 720, "mode": "windowed"},
                                           "base_resolution": [320, 180], "stretch": "viewport", "aspect": "keep",
                                           "integer_scale": True}, indent=2) + "\n")
write("NoorPlatformer.noorproj", json.dumps({"noor_project": 1, "id": gid("project"), "name": "NoorPlatformer",
                                             "engine": "0.0.1", "created": "2026-11-21T12:00:00Z"}, indent=2) + "\n")
write(".gitignore", "# Derived data, rebuilt on demand\n.noor/\n*.tmp\n")
write("Scenes/Level.noorscene", json.dumps({"noor_scene": 2, "id": gid("scene"), "name": "Level", "entities": ents}, indent=2) + "\n")
write("README.md", """# NoorPlatformer

A short pixel-art platformer: the 2D foundations' sample ([plan](../../docs/milestones/M9_2D_PLAN.md)). Run, jump, collect
the eight coins, mind the spikes, and reach the door.

    noor_player samples/NoorPlatformer

**A / D** or **← / →** run, **Space** jumps (hold for higher), **↓ + Space** drops through a plank. Until you press
a key, an autopilot plays.

What it's made of, all public features you can use in your own game:

- A **tilemap** level (`Scenes/Level.noorscene`, tileset `Assets/Tiles/level.noortileset`): solid stone and brick,
  one-way planks, decoration. Edit it in the editor's 2D view with the Tilemaps panel.
- The built-in **`platformer`** character (speed, jump height, coyote time, jump buffering, animations).
- **Triggers**: coins, spikes and the door are trigger colliders; `Scripts/Hero.luau` reacts in `on_trigger_enter`.
- A **HUD** (`noor.ui.text`, `noor.ui.rect`), laid out in the game's 320 × 180 pixels.
- **Display settings** (`Settings/display.json`): drawn at 320 × 180 and scaled up in whole steps (4 × 4 screen pixels
  per game pixel at 1280 × 720), black bars where the shape differs ([Display](../../docs/rendering/DISPLAY.md)).
- A **follow camera** (dead zone, smoothing, the level's bounds, snapped to pixels) and **parallax** mountains and pines.

The art is Tom's own, hand-drawn for an old game of his and used here with his permission. The level, scene and script
are written by `tools/samples/make_noorplatformer.py`.
""")
print("NoorPlatformer written to", PROJECT)
