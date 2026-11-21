"""NoorIsometric: the 2D foundations' isometric sample (2D part 7).

    python tools/samples/make_noorisometric.py      (needs Pillow)

Writes samples/NoorIsometric: a small torch-lit dungeon room in the classic 2:1 isometric view, built only from public
engine features: an isometric tilemap (floor diamonds and tall wall blocks, drawn here), Coth from The Uncounted walking
in eight directions ("facing") and passing behind pillars and in front of them ("y_depth"), a follow camera, point
lights, and display settings for smooth (not pixel) art. One Lua script is the whole game: walking on the floor cells
only, and an autopilot that walks a loop until a key is pressed.

  NoorIsometric.noorproj, .gitignore, README.md
  Settings/display.json             canvas stretch at 1280 x 720 (sharp at any size)
  Assets/Sprites/dungeon.png        floor diamonds and wall blocks, 128 x 192 pixels each, drawn here (ours)
  Assets/Sprites/coth.png           Coth: 8 directions, idle and walk (Tom's own art for The Uncounted)
  Assets/Tiles/dungeon.noortileset
  Scripts/Walker.luau               walking, facing, the autopilot
  Scenes/Room.noorscene             the room
"""
import hashlib
import json
import math
import pathlib
import random
import shutil
import uuid

from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parents[2]
PROJECT = ROOT / "samples" / "NoorIsometric"
NS = uuid.UUID("9a07e000-0000-4000-8000-0000000150fe")
PPM = 64
CW, CH = 2.0, 1.0  # the cell: a 2 m x 1 m diamond (128 x 64 pixels)


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


def texture(rel, img=None):
    p = PROJECT / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    if img is not None:
        img.save(p)
    meta(p, gid(rel), "texture", {"kind": "color", "max_size": 4096, "mips": True, "normal_convention": "opengl"})
    return gid(rel)


def sheet(rel, tex, columns, rows, pivot, ppm, filt, animations=None, directions=None):
    s = {"noor_sprite": 1, "name": pathlib.Path(rel).stem, "texture": tex,
         "grid": {"columns": columns, "rows": rows, "margin": 0, "spacing": 0},
         "pivot": pivot, "pixels_per_metre": ppm, "filter": filt, "animations": animations or {}}
    if directions:
        s["directions"] = directions
    p = write(rel, json.dumps(s, indent=2) + "\n")
    meta(p, gid(rel), "sprite", {})
    return gid(rel)


# ---- The tiles: 128 x 192 frames; the 128 x 64 diamond at the bottom, walls rising 128 pixels above it ----------------
FW, FH = 128, 192
rnd = random.Random(7)


def shade(c, k):
    return tuple(max(0, min(255, int(v * k))) for v in c[:3]) + (255,)


def stone_noise(img, box_poly, base, amount=0.10, count=900):
    """Speckles of lighter and darker stone inside a polygon (a mask), for texture."""
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).polygon(box_poly, fill=255)
    px = img.load()
    m = mask.load()
    xs = [p[0] for p in box_poly]
    ys = [p[1] for p in box_poly]
    for _ in range(count):
        x = rnd.randint(int(min(xs)), int(max(xs)) - 1)
        y = rnd.randint(int(min(ys)), int(max(ys)) - 1)
        if m[x, y]:
            k = 1.0 + rnd.uniform(-amount, amount)
            px[x, y] = shade(base, k)


def floor_tile(ox, base, cracks=0):
    img = Image.new("RGBA", (FW, FH), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cy = FH - 32
    dia = [(0, cy), (64, cy - 32), (127, cy), (64, cy + 31)]
    d.polygon(dia, fill=base)
    stone_noise(img, dia, base)
    # flagstone seams: two lines across the diamond
    seam = shade(base, 0.72)
    d.line([(32, cy - 16), (96, cy + 16)], fill=seam, width=1)
    d.line([(32, cy + 16), (96, cy - 16)], fill=seam, width=1)
    for _ in range(cracks):
        x, y = rnd.randint(40, 88), cy + rnd.randint(-10, 10)
        d.line([(x, y), (x + rnd.randint(-10, 10), y + rnd.randint(-6, 6))], fill=shade(base, 0.6))
    d.line(dia + [dia[0]], fill=shade(base, 0.8), width=1)
    return img


def wall_tile(base, height=128):
    img = Image.new("RGBA", (FW, FH), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cy = FH - 32
    top = cy - height
    left = [(0, cy), (64, cy + 31), (64, cy + 31 - height), (0, cy - height)]
    right = [(64, cy + 31), (127, cy), (127, cy - height), (64, cy + 31 - height)]
    lid = [(0, top), (64, top - 32), (127, top), (64, top + 31)]
    d.polygon(left, fill=shade(base, 0.85))
    d.polygon(right, fill=shade(base, 0.62))
    d.polygon(lid, fill=shade(base, 1.15))
    stone_noise(img, left, shade(base, 0.85), count=700)
    stone_noise(img, right, shade(base, 0.62), count=700)
    # brick courses on the faces: 16 pixels high, staggered
    for row in range(height // 16):
        y0 = cy - 16 * (row + 1)
        d.line([(0, y0), (64, y0 + 32)], fill=shade(base, 0.6))
        d.line([(64, y0 + 32), (127, y0)], fill=shade(base, 0.45))
        off = 16 if row % 2 else 0
        for bx in range(off, 64, 32):
            d.line([(bx, y0 + bx // 2), (bx, y0 + bx // 2 + 16)], fill=shade(base, 0.6))
        for bx in range(64 + off, 128, 32):
            yy = y0 + 32 - (bx - 64) // 2
            d.line([(bx, yy), (bx, yy + 16)], fill=shade(base, 0.45))
    d.line(lid + [lid[0]], fill=shade(base, 0.7))
    return img


FLOOR, FLOOR2, FLOOR3, WALL, PILLAR, LOW = range(6)
frames = [floor_tile(0, (112, 104, 96, 255)), floor_tile(0, (104, 98, 92, 255), cracks=3), floor_tile(0, (96, 92, 90, 255), cracks=1),
          wall_tile((120, 100, 88, 255)), wall_tile((132, 116, 104, 255), height=112), wall_tile((120, 100, 88, 255), height=40)]
atlas = Image.new("RGBA", (FW * len(frames), FH), (0, 0, 0, 0))
for i, f in enumerate(frames):
    atlas.paste(f, (i * FW, 0))
dungeon_sheet = sheet("Assets/Sprites/dungeon.noorsprite", texture("Assets/Sprites/dungeon.png", atlas), len(frames), 1,
                      [0.5, 32 / FH], PPM, "linear")
tileset = {"noor_tileset": 1, "name": "Dungeon", "sheet": dungeon_sheet, "solid": [], "one_way": [], "floor": [FLOOR, FLOOR2, FLOOR3]}
p = write("Assets/Tiles/dungeon.noortileset", json.dumps(tileset, indent=2) + "\n")
meta(p, gid("Assets/Tiles/dungeon.noortileset"), "tileset", {})
TILESET = gid("Assets/Tiles/dungeon.noortileset")

# Coth (The Uncounted, Tom's art; cleared for publishing): the test data's sheet.
src = ROOT / "tests" / "data" / "render_scenes" / "Assets" / "Sprites" / "coth.png"
(PROJECT / "Assets" / "Sprites").mkdir(parents=True, exist_ok=True)
shutil.copyfile(src, PROJECT / "Assets" / "Sprites" / "coth.png")
coth_sheet = sheet("Assets/Sprites/coth.noorsprite", texture("Assets/Sprites/coth.png"), 8, 6, [0.5, 0.078125], 64, "linear", {
    "idle": {"frames": [0], "fps": 2, "loop": True},
    "walk": {"frames": [8, 16], "fps": 6, "loop": True}}, directions=8)

# ---- The room: 12 x 12 cells; walls along the far edges, pillars and a low wall inside ---------------------------------
N = 12
grid = {}
for x in range(N):
    for y in range(N):
        grid[(x, y)] = rnd.choice([FLOOR, FLOOR, FLOOR, FLOOR2, FLOOR3])
for i in range(N):
    grid[(N - 1, i)] = WALL
    grid[(i, N - 1)] = WALL
for c in [(4, 4), (4, 7), (7, 4), (7, 7)]:
    grid[c] = PILLAR
for x in range(2, 4):
    grid[(x, 9)] = LOW
rows = [" ".join(str(grid[(x, y)]) for x in range(N)) for y in range(N - 1, -1, -1)]


def centre(x, y):
    return ((x - y) * CW * 0.5, (x + y) * CH * 0.5)


ents = []


def entity(name, pos, comps, parent=None):
    e = {"id": gid("e " + name), "name": name, "layer": "default", "tags": [], "parent": parent,
         "transform": {"position": list(pos), "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]}, "components": comps}
    ents.append(e)
    return e


START = centre(2, 2)
entity("Camera", (START[0], START[1] + 1, 20), {
    "camera": {"projection": "orthographic", "height": 9, "near": 0.1, "far": 60, "ev100": 6},
    "camera_follow": {"target": "Coth", "offset": [0, 1], "dead_zone": [2, 1], "smoothing": 0.25}})
entity("Moonlight", (0, 0, 30), {"light": {"type": "directional", "color": "#8090C0", "illuminance": 700, "shadows": False,
                                           "sky_color": "#202030", "sky_luminance": 1}})
entity("Room", (0, 0, 0), {"tilemap": {"tileset": TILESET, "rows": rows, "layout": "isometric", "cell": [CW, CH]}})
for i, (x, y) in enumerate([(10, 3), (10, 8), (3, 10), (8, 10)]):
    cx, cy = centre(x, y)
    entity(f"Torch {i + 1}", (cx, cy + 1.6, 1.5), {"light": {"type": "point", "color": "#FFB060", "intensity": 6000, "range": 9}})
walker = entity("Coth", (START[0], START[1], 0), {
    "sprite": {"sheet": coth_sheet, "animation": "idle", "billboard": "none", "cast_shadows": False, "y_depth": True},
    "script": {"file": "Scripts/Walker.luau", "params": {"map": "Room"}}})
entity("Lantern", (0, 1.2, 1.2), {"light": {"type": "point", "color": "#FFD9A0", "intensity": 2500, "range": 6}}, parent=walker["id"])

write("Scripts/Walker.luau", r'''-- NoorIsometric: walking in an isometric room. W A S D or the arrows walk on screen (up is up), only onto floor cells;
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
''')

write("Settings/display.json", json.dumps({"window": {"width": 1280, "height": 720, "mode": "windowed"},
                                           "base_resolution": [1280, 720], "stretch": "canvas", "aspect": "keep"}, indent=2) + "\n")
write("NoorIsometric.noorproj", json.dumps({"noor_project": 1, "id": gid("project"), "name": "NoorIsometric",
                                            "engine": "0.0.1", "created": "2026-11-21T12:00:00Z"}, indent=2) + "\n")
write(".gitignore", "# Derived data, rebuilt on demand\n.noor/\n*.tmp\n")
write("Scenes/Room.noorscene", json.dumps({"noor_scene": 2, "id": gid("scene"), "name": "Room", "entities": ents}, indent=2) + "\n")
write("README.md", """# NoorIsometric

A small torch-lit dungeon room in the classic 2:1 isometric view: the 2D foundations' isometric sample
([plan](../../docs/milestones/M9_2D_PLAN.md)). Coth, from Tom's game The Uncounted, walks round the pillars.

    noor_player samples/NoorIsometric

**W A S D** or the **arrows** walk (up is up on screen). Until you press a key, Coth walks a loop by himself.

What it's made of, all public features:

- An **isometric tilemap** (`"layout": "isometric"`, 2 × 1 m diamonds): floor flagstones and tall wall blocks, drawn by
  `tools/samples/make_noorisometric.py`. Nearer tiles hide the ones behind.
- **Coth**, an 8-direction sprite sheet. In a flat game the script says which way he faces on screen (`"facing"`), and
  the sheet shows that view. **`"y_depth": true`** gives him the map's depth rule, so a pillar in front of him hides
  him and he hides a pillar behind him, pixel by pixel ([Sprites](../../docs/fx/SPRITES.md)).
- **Walking on the floor only**: isometric maps don't collide (yet), so `Scripts/Walker.luau` asks the map which tile
  is under the next step (`e:tile(x, y)`) and slides along walls.
- **Lighting**: moonlight, four torches and a lantern that walks with Coth (point lights; sprites and tiles are lit).
- A **follow camera** and **display settings** for smooth art (`canvas`: sharp at any window size).
""")
print("NoorIsometric written to", PROJECT)
