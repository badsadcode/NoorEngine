"""NoorEngine's first project templates (2D foundations, part 7; spec 24.1).

    python tools/samples/make_templates.py      (needs Pillow)

Writes templates/:
  Empty2D/       Empty 2D: an orthographic camera, display settings, a sprite and a script to start from
  Platformer2D/  2D Platformer: a pixel-art tilemap level, the built-in platformer character, a follow camera
  TopDown2D/     Top-Down 2D: a tilemap with water and trees that block, a character you steer in every direction
                 (your own controller: "motion": "script"), a follow camera

Each is a small working start made only of public features (no template-only tricks). `noor_cli new <folder> <name>
--template <id>` and the editor's New Project make projects from them. The art is drawn here, pixel by pixel (ours).
"""
import hashlib
import json
import pathlib
import shutil
import uuid

from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parents[2]
TEMPLATES = ROOT / "templates"


class Template:
    def __init__(self, tid, name, description, order, ns):
        self.dir = TEMPLATES / tid
        if self.dir.exists():
            shutil.rmtree(self.dir)
        self.ns = uuid.UUID(ns)
        self.ents = []
        self.write("template.json", json.dumps({"noor_template": 1, "name": name, "description": description, "order": order}, indent=2) + "\n")

    def gid(self, name):
        return str(uuid.uuid5(self.ns, name))

    def write(self, rel, text):
        p = self.dir / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text, encoding="utf-8", newline="\n")
        return p

    def meta(self, path, guid, importer, settings):
        sha = hashlib.sha256(path.read_bytes()).hexdigest()
        m = {"guid": guid, "importer": importer, "noor_meta": 1, "settings": settings, "source_sha256": sha}
        path.with_name(path.name + ".noormeta").write_text(json.dumps(m, indent=2) + "\n", newline="\n")

    def texture(self, img, rel):
        p = self.dir / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        img.save(p)
        # Pixel art: no compression, so every colour stays exact.
        self.meta(p, self.gid(rel), "texture", {"kind": "color", "max_size": 4096, "mips": False, "normal_convention": "opengl", "compression": "none"})
        return self.gid(rel)

    def sheet(self, rel, img, columns, rows, pivot, animations=None, directions=None, ppm=16):
        tex = self.texture(img, rel.replace(".noorsprite", ".png"))
        s = {"noor_sprite": 1, "name": pathlib.Path(rel).stem, "texture": tex,
             "grid": {"columns": columns, "rows": rows, "margin": 0, "spacing": 0},
             "pivot": pivot, "pixels_per_metre": ppm, "filter": "nearest", "animations": animations or {}}
        if directions:
            s["directions"] = directions
        p = self.write(rel, json.dumps(s, indent=2) + "\n")
        self.meta(p, self.gid(rel), "sprite", {})
        return self.gid(rel)

    def tileset(self, rel, sheet_id, solid, one_way=()):
        p = self.write(rel, json.dumps({"noor_tileset": 1, "name": pathlib.Path(rel).stem, "sheet": sheet_id,
                                        "solid": list(solid), "one_way": list(one_way)}, indent=2) + "\n")
        self.meta(p, self.gid(rel), "tileset", {})
        return self.gid(rel)

    def entity(self, name, pos, comps, parent=None, scale=(1, 1, 1)):
        e = {"id": self.gid("e " + name), "name": name, "layer": "default", "tags": [], "parent": parent,
             "transform": {"position": list(pos), "rotation": [0, 0, 0, 1], "scale": list(scale)}, "components": comps}
        self.ents.append(e)
        return e

    def finish(self, display, readme):
        self.write("Scenes/Main.noorscene", json.dumps({"noor_scene": 2, "id": self.gid("scene"), "name": "Main", "entities": self.ents}, indent=2) + "\n")
        self.write("Settings/display.json", json.dumps(display, indent=2) + "\n")
        self.write("README.md", readme)


def sprite(sheet_id, **extra):
    s = {"sheet": sheet_id, "billboard": "none", "cast_shadows": False, "unlit": True}
    s.update(extra)
    return {"sprite": s}


def camera(height, at, **extra):
    c = {"camera": {"projection": "orthographic", "height": height, "near": 0.1, "far": 60, "ev100": 14, "pixel_snap": True}}
    c.update(extra)
    return c


def rows_of(grid, w, h):
    return [" ".join(str(grid[(x, y)]) if (x, y) in grid else "." for x in range(w)) for y in range(h - 1, -1, -1)]


PIXEL_DISPLAY = {"window": {"width": 1280, "height": 720, "mode": "windowed"}, "base_resolution": [320, 180],
                 "stretch": "viewport", "aspect": "keep", "integer_scale": True}


def px(img, pts, colour):
    d = img.load()
    for x, y in pts:
        d[x, y] = colour


# ---- Empty 2D -------------------------------------------------------------------------------------------------------
def empty_2d():
    t = Template("Empty2D", "Empty 2D", "An orthographic camera, pixel-perfect display settings, one sprite and one script. "
                 "Start here for any 2D game.", 10, "9a07f000-0000-4000-8000-0000000e2d00")
    icon = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)
    d.polygon([(8, 1), (14, 8), (8, 15), (2, 8)], fill=(160, 124, 255, 255), outline=(242, 238, 255, 255))
    d.polygon([(8, 5), (11, 8), (8, 11), (5, 8)], fill=(255, 200, 87, 255))
    icon_sheet = t.sheet("Assets/Sprites/icon.noorsprite", icon, 1, 1, [0.5, 0.5])
    t.entity("Camera", (0, 0, 20), camera(180 / 16, (0, 0)))
    t.entity("Icon", (0, 0, 0), dict(sprite(icon_sheet), script={"file": "Scripts/Main.luau", "params": {"speed": 90}}))
    t.write("Scripts/Main.luau", '''-- A first script: it turns the sprite it's on and writes on the screen. Change it, press Play, and see.
local Main = {}

function Main:on_update(dt)
    self.entity:rotate(vector.create(0, 0, 1), self.params.speed * dt)  -- degrees a second, from the script's params
    noor.ui.text("Hello, 2D!", 0, 40, { size = 48, anchor = "top" })
end

return Main
''')
    t.finish(PIXEL_DISPLAY, """# Empty 2D

A clean start for a 2D game:

- `Scenes/Main.noorscene`: an orthographic camera (2D: the XY plane, x right, y up, metres) and one sprite.
- `Scripts/Main.luau`: a script on the sprite. It turns it and writes on the screen.
- `Settings/display.json`: drawn at 320 × 180 and scaled up in whole pixels, crisp pixel art at any window size.
  Change `base_resolution`, or `"stretch": "canvas"` for smooth art.
- `Assets/Sprites/icon.png`: 16 pixels a metre, nearest filtering.

Next: the [2D docs](https://github.com/badsadcode/NoorEngine/blob/main/docs/milestones/M9_2D_PLAN.md) (sprites,
tilemaps, physics), and [Coming from Godot](https://github.com/badsadcode/NoorEngine/blob/main/docs/scripting/COMING_FROM_GODOT.md).
""")


# ---- 2D Platformer --------------------------------------------------------------------------------------------------
def platformer_2d():
    t = Template("Platformer2D", "2D Platformer", "A pixel-art tilemap level and a character that runs and jumps (coyote "
                 "time, jump buffering, one-way platforms), with a follow camera.", 20, "9a07f000-0000-4000-8000-0000000e2d01")
    # Tiles (16 x 16): 0 grass top, 1 dirt, 2 stone, 3 plank (one-way), 4 flower, 5 grass tuft.
    tiles = Image.new("RGBA", (16 * 6, 16), (0, 0, 0, 0))
    d = ImageDraw.Draw(tiles)
    d.rectangle([0, 4, 15, 15], fill=(134, 90, 56, 255))
    d.rectangle([0, 0, 15, 5], fill=(96, 180, 76, 255))
    px(tiles, [(x, 6) for x in range(0, 16, 3)], (96, 180, 76, 255))
    d.rectangle([16, 0, 31, 15], fill=(134, 90, 56, 255))
    px(tiles, [(18, 3), (25, 6), (21, 11), (29, 13), (27, 2)], (110, 72, 44, 255))
    d.rectangle([32, 0, 47, 15], fill=(120, 120, 136, 255))
    d.line([(32, 7), (47, 7)], fill=(92, 92, 108, 255))
    d.line([(39, 0), (39, 7)], fill=(92, 92, 108, 255))
    d.line([(43, 8), (43, 15)], fill=(92, 92, 108, 255))
    d.rectangle([48, 0, 63, 4], fill=(176, 124, 72, 255))
    d.line([(48, 4), (63, 4)], fill=(120, 80, 44, 255))
    d.line([(52, 5), (52, 7)], fill=(120, 80, 44, 255))
    d.line([(59, 5), (59, 7)], fill=(120, 80, 44, 255))
    d.line([(72, 15), (72, 8)], fill=(70, 140, 60, 255))
    d.ellipse([70, 5, 74, 9], fill=(255, 107, 107, 255))
    for x in (82, 85, 88, 91):
        d.line([(x, 15), (x + (x % 3) - 1, 10)], fill=(96, 180, 76, 255))
    tiles_sheet = t.sheet("Assets/Sprites/tiles.noorsprite", tiles, 6, 1, [0.5, 0.5])
    tileset = t.tileset("Assets/Tiles/level.noortileset", tiles_sheet, solid=[0, 1, 2], one_way=[3])
    # The player (16 x 16 frames): idle 0-1, run 2-5, jump 6. A round little hero with a scarf.
    hero = Image.new("RGBA", (16 * 7, 16), (0, 0, 0, 0))
    for f in range(7):
        ox = f * 16
        hd = ImageDraw.Draw(hero)
        bob = 1 if f in (1, 3, 5) else 0
        hd.ellipse([ox + 4, 3 + bob, ox + 12, 12 + bob], fill=(107, 184, 255, 255), outline=(40, 60, 110, 255))
        hd.rectangle([ox + 4, 8 + bob, ox + 12, 9 + bob], fill=(255, 107, 107, 255))
        px(hero, [(ox + 9, 6 + bob), (ox + 11, 6 + bob)], (20, 20, 40, 255))
        legs = {0: (6, 10), 1: (6, 10), 2: (5, 11), 3: (7, 9), 4: (5, 11), 5: (7, 9), 6: (4, 12)}[f]
        for lx in legs:
            hd.line([(ox + lx, 13), (ox + lx, 15)], fill=(40, 60, 110, 255))
    hero_sheet = t.sheet("Assets/Sprites/player.noorsprite", hero, 7, 1, [0.5, 0.0], {
        "idle": {"frames": [0, 1], "fps": 3, "loop": True}, "run": {"frames": [2, 3, 4, 5], "fps": 10, "loop": True},
        "jump": {"frames": [6], "fps": 5, "loop": True}, "fall": {"frames": [6], "fps": 5, "loop": True}})
    W, H = 40, 12
    g = {}
    for x in range(W):
        g[(x, 0)] = 1
        g[(x, 1)] = 0
    for x in range(14, 18):
        g[(x, 1)] = 2
        g[(x, 2)] = 2
    g[(14, 2)] = g[(15, 2)] = g[(16, 2)] = g[(17, 2)] = 2
    for x in range(6, 10):
        g[(x, 4)] = 3
    for x in range(21, 25):
        g[(x, 5)] = 3
    for x in range(28, 31):
        g[(x, 3)] = 2
    for y in range(2, H):
        g[(0, y)] = 2
        g[(W - 1, y)] = 2
    for x in (3, 11, 26, 34):
        g[(x, 2)] = 4 if x % 2 else 5
    t.entity("Camera", (8, 6.125, 20), camera(180 / 16, (8, 6), camera_follow={
        "target": "Player", "offset": [0, 1.5], "dead_zone": [3, 2], "smoothing": 0.15, "bounds": [-0.5, -0.5, W - 0.5, H + 2]}))
    sky = t.sheet("Assets/Sprites/sky.noorsprite", Image.new("RGBA", (1, 1), (126, 200, 240, 255)), 1, 1, [0.5, 0.5], ppm=1)
    t.entity("Sky", (0, 0, -5), dict(sprite(sky, layer=-1), parallax={"factor": [0, 0], "anchor": [0, 0]}), scale=(80, 40, 1))
    t.entity("Level", (0, 0, 0), {"tilemap": {"tileset": tileset, "rows": rows_of(g, W, H), "unlit": True}})
    t.entity("Player", (3, 1.51, 0.1), dict(sprite(hero_sheet, animation="idle", order=1), **{
        "character": {"radius": 0.3, "height": 0.85, "plane": "xy", "gravity_scale": 3, "acceleration": 40, "deceleration": 50,
                      "air_acceleration": 25, "coyote_time": 0.1, "jump_buffer": 0.1},
        "platformer": {"speed": 6, "jump_height": 2.2, "animations": {"idle": "idle", "run": "run", "jump": "jump", "fall": "fall"}},
        "script": {"file": "Scripts/Player.luau"}}))
    t.write("Scripts/Player.luau", '''-- The built-in `platformer` component already runs and jumps (A/D or arrows, Space; down + Space drops through a
-- plank). This script is where your game starts: here it only shows the keys. Add coins, enemies, a goal...
-- For a controller of your own instead, see "Your own controller" in the Lua docs ("motion": "script").
local Player = {}

function Player:on_update(dt)
    if noor.time() < 5 then
        noor.ui.text("A / D or arrows to run, Space to jump", 0, -24, { size = 36, anchor = "bottom" })
    end
end

return Player
''')
    t.finish(PIXEL_DISPLAY, """# 2D Platformer

A small level to run and jump in, to build your platformer from:

- `Scenes/Main.noorscene`: a **tilemap** level (`Assets/Tiles/level.noortileset`: grass, dirt and stone are solid,
  planks are one-way), the player, a sky, and a camera that **follows** (dead zone, smoothing, the level's edges).
- The player is a `character` in the XY plane with the built-in **`platformer`** component: speed, jump height, coyote
  time and jump buffering are all on the component. `Scripts/Player.luau` is yours to grow.
- `Settings/display.json`: 320 × 180, scaled up in whole pixels.

Paint more level in the editor: the **2D** view and the **Tilemaps** tab. Docs: tilemaps, sprites, the platformer
character and your own controllers, in the repository's `docs/`.
""")


# ---- Top-Down 2D ----------------------------------------------------------------------------------------------------
def top_down_2d():
    t = Template("TopDown2D", "Top-Down 2D", "A tilemap meadow with water and trees that block, and a character you steer "
                 "in every direction with your own script, with a follow camera.", 30, "9a07f000-0000-4000-8000-0000000e2d02")
    # Tiles: 0 grass, 1 grass 2, 2 path, 3 water (solid), 4 tree (solid), 5 rock (solid).
    tiles = Image.new("RGBA", (16 * 6, 16), (0, 0, 0, 0))
    d = ImageDraw.Draw(tiles)
    d.rectangle([0, 0, 15, 15], fill=(98, 170, 84, 255))
    d.rectangle([16, 0, 31, 15], fill=(98, 170, 84, 255))
    px(tiles, [(19, 4), (20, 3), (26, 10), (27, 9), (23, 13)], (70, 140, 60, 255))
    d.rectangle([32, 0, 47, 15], fill=(204, 176, 120, 255))
    px(tiles, [(35, 5), (42, 11), (38, 13)], (176, 150, 100, 255))
    d.rectangle([48, 0, 63, 15], fill=(64, 120, 200, 255))
    d.line([(51, 5), (55, 5)], fill=(140, 190, 240, 255))
    d.line([(56, 11), (60, 11)], fill=(140, 190, 240, 255))
    d.rectangle([64, 0, 79, 15], fill=(98, 170, 84, 255))
    d.ellipse([65, 1, 78, 13], fill=(40, 110, 56, 255), outline=(28, 80, 40, 255))
    d.rectangle([70, 12, 73, 15], fill=(110, 72, 44, 255))
    d.rectangle([80, 0, 95, 15], fill=(98, 170, 84, 255))
    d.ellipse([82, 4, 93, 14], fill=(140, 140, 150, 255), outline=(96, 96, 108, 255))
    tiles_sheet = t.sheet("Assets/Sprites/tiles.noorsprite", tiles, 6, 1, [0.5, 0.5])
    tileset = t.tileset("Assets/Tiles/meadow.noortileset", tiles_sheet, solid=[3, 4, 5])
    # The player: 4 directions (columns: down, left, up, right) x 2 walk frames (rows).
    hero = Image.new("RGBA", (16 * 4, 16 * 2), (0, 0, 0, 0))
    for row in range(2):
        for col in range(4):
            ox, oy = col * 16, row * 16
            hd = ImageDraw.Draw(hero)
            hd.ellipse([ox + 3, oy + 3, ox + 13, oy + 13], fill=(255, 200, 87, 255), outline=(140, 96, 30, 255))
            eyes = {0: [(6, 8), (10, 8)], 1: [(5, 8)], 2: [], 3: [(11, 8)]}[col]
            px(hero, [(ox + x, oy + y) for x, y in eyes], (40, 30, 20, 255))
            step = 1 if row else 0
            hd.line([(ox + 6 - step, oy + 13), (ox + 6 - step, oy + 15)], fill=(140, 96, 30, 255))
            hd.line([(ox + 10 + step, oy + 13), (ox + 10 + step, oy + 15)], fill=(140, 96, 30, 255))
    hero_sheet = t.sheet("Assets/Sprites/player.noorsprite", hero, 4, 2, [0.5, 0.3], {
        "idle": {"frames": [0], "fps": 2, "loop": True}, "walk": {"frames": [0, 4], "fps": 6, "loop": True}}, directions=4)
    W, H = 30, 20
    g = {}
    for x in range(W):
        for y in range(H):
            g[(x, y)] = 1 if (x * 7 + y * 3) % 5 == 0 else 0
    for x in range(W):
        g[(x, 0)] = g[(x, H - 1)] = 4
    for y in range(H):
        g[(0, y)] = g[(W - 1, y)] = 4
    for x in range(3, 27):
        g[(x, 9)] = 2
    for x in range(16, 22):
        for y in range(12, 16):
            g[(x, y)] = 3
    for c in [(5, 4), (6, 4), (9, 14), (12, 5), (24, 4), (25, 13), (7, 15)]:
        g[c] = 4
    for c in [(14, 3), (20, 6), (4, 12)]:
        g[c] = 5
    t.entity("Camera", (5, 9, 20), camera(180 / 16, (5, 9), camera_follow={
        "target": "Player", "dead_zone": [2, 1.5], "smoothing": 0.12, "bounds": [-0.5, -0.5, W - 0.5, H - 0.5]}))
    t.entity("Meadow", (0, 0, 0), {"tilemap": {"tileset": tileset, "rows": rows_of(g, W, H), "unlit": True}})
    t.entity("Player", (4, 9, 0.1), dict(sprite(hero_sheet, animation="idle", order=1), **{
        "character": {"radius": 0.3, "height": 0.6, "plane": "xy", "motion": "script"},
        "script": {"file": "Scripts/Player.luau", "params": {"speed": 4}}}))
    t.write("Scripts/Player.luau", '''-- A top-down character you steer yourself: "motion": "script" on its character means NoorEngine adds no gravity and
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
''')
    t.finish(PIXEL_DISPLAY, """# Top-Down 2D

A meadow to walk round, to build a top-down game from (an adventure, a farm, a dungeon crawler):

- `Scenes/Main.noorscene`: a **tilemap** (`Assets/Tiles/meadow.noortileset`: water, trees and rocks are solid), the
  player, and a camera that **follows** inside the map's edges.
- The player is a `character` in the XY plane with **`"motion": "script"`**: `Scripts/Player.luau` owns its velocity
  (Godot's CharacterBody2D way), so it walks in every direction and slides along what's solid. The sprite sheet has
  four views; the script's `"facing"` picks one.
- `Settings/display.json`: 320 × 180, scaled up in whole pixels.

Paint the map in the editor: the **2D** view and the **Tilemaps** tab.
""")


if __name__ == "__main__":
    empty_2d()
    platformer_2d()
    top_down_2d()
    print("templates written to", TEMPLATES)
