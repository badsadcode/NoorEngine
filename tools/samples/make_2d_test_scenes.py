"""Test scenes for 2D (the 2D foundations, ADR-040).

    python tools/samples/make_2d_test_scenes.py      (needs Pillow)

Writes into tests/data/render_scenes:
  Scenes/Physics2D_Pile.noorscene   part 1: rectangles and circles locked to the XY plane, dropped onto a tilted floor
                                    against a wall, seen by an orthographic camera (a golden four seconds in)
  Scenes/Sprites2D_Layers.noorscene part 3: walkers at one depth drawn by sorting layer, not by scene order (red in
                                    front, then blue, green behind); a row of coins by "order"; soft-edged glows over
                                    them; a lit and an unlit coin side by side under a dim sun
  Scenes/Sprites2D_YSort.noorscene  part 3: a top-down crowd sorted by height on screen ("sort": "y"): lower is nearer,
                                    whatever order the scene lists them in
  Scenes/Sprites2D_Bleed.noorscene  part 3: a frame next to a solid red one, magnified: no red at its edges
  Assets/Sprites/glow.png, .noorsprite      a soft round glow (alpha falling off smoothly), drawn here
  Assets/Sprites/bleed.png, .noorsprite     two 8 x 8 frames: a green square with a clear border, then solid red
  Scenes/Tilemap_Square.noorscene   part 4: a square tilemap (ground, a brick wall, one-way planks, flowers) with a walker
  Scenes/Tilemap_Iso.noorscene      part 4: an isometric map: grass diamonds and tall stone blocks drawn back to front
  Scenes/Tilemap_Hex.noorscene      part 4: a hexagonal map of grass, sand and water
  Scenes/Tilemap_Big.noorscene      part 4: 256 x 256 square tiles, for what a big map costs (64 chunks)
  Assets/Tiles/*.png, *.noorsprite, *.noortileset   the tiles for those, drawn here pixel by pixel
"""
import hashlib
import json
import math
import pathlib
import uuid

import random

from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parents[2] / "tests" / "data" / "render_scenes"
NS = uuid.UUID("9a02d000-0000-4000-8000-0000000000d2")
COLOURS = ["#A07CFF", "#FFC857", "#5FD3A8", "#FF6B6B", "#6BB8FF", "#F2EEFF"]
WALKER = "5b417e00-0001-4000-8000-000000000001"  # make_test_sprites.py's sheet: walk 0-3, coin 4-7, 2 m tall


def gid(name):
    return str(uuid.uuid5(NS, name))


def z_turn(degrees):
    h = math.radians(degrees) * 0.5
    return [0.0, 0.0, math.sin(h), math.cos(h)]


def entity(name, position, components, rotation=(0, 0, 0, 1)):
    return {"id": gid(name), "name": name, "layer": "default", "tags": [], "parent": None,
            "transform": {"position": list(position), "rotation": list(rotation), "scale": [1, 1, 1]},
            "components": components}


def write_scene(name, ents):
    s = {"noor_scene": 2, "id": gid("scene " + name), "name": name, "entities": ents}
    (ROOT / "Scenes" / f"{name}.noorscene").write_text(json.dumps(s, indent=2) + "\n", encoding="utf-8", newline="\n")


def meta(path, guid, importer, settings):
    sha = hashlib.sha256(path.read_bytes()).hexdigest()
    m = {"guid": guid, "importer": importer, "noor_meta": 1, "settings": settings, "source_sha256": sha}
    path.with_name(path.name + ".noormeta").write_text(json.dumps(m, indent=2) + "\n", newline="\n")


def sheet(name, img, columns, rows, pivot, ppm, filt):
    png = ROOT / "Assets" / "Sprites" / f"{name}.png"
    img.save(png)
    meta(png, gid(name + ".png"), "texture", {"kind": "color", "max_size": 4096, "mips": False, "normal_convention": "opengl"})
    s = {"noor_sprite": 1, "name": name, "texture": gid(name + ".png"), "grid": {"columns": columns, "rows": rows, "margin": 0, "spacing": 0},
         "pivot": pivot, "pixels_per_metre": ppm, "filter": filt, "animations": {}}
    p = ROOT / "Assets" / "Sprites" / f"{name}.noorsprite"
    p.write_text(json.dumps(s, indent=2) + "\n", encoding="utf-8", newline="\n")
    meta(p, gid(name + ".noorsprite"), "sprite", {})
    return gid(name + ".noorsprite")


def camera_and_sun(height, illuminance=90000, at=(0, 0, 20)):
    return [
        entity("Camera", at, {"camera": {"projection": "orthographic", "height": height, "near": 0.1, "far": 60, "ev100": 14}}),
        entity("Sun", (0, 0, 30), {"light": {"type": "directional", "color": "#FFFFFF", "illuminance": illuminance, "shadows": False,
                                             "sky_color": "#B8B0FF", "sky_luminance": 3000}}),
        entity("Backdrop", (0, 0, -2), {"primitive": {"shape": "box", "size": [80, 50, 0.2], "color": "#141127", "roughness": 0.9}}),
    ]


def pile():
    ents = camera_and_sun(14, at=(0, 3, 20))
    ents.append(entity("Ground", (0, -2, 0), {"primitive": {"shape": "box", "size": [24, 1, 1], "color": "#4A4370"},
                                              "collider": {"shape": "rectangle", "size": [24, 1]}}, z_turn(-12)))
    ents.append(entity("Wall", (8, 2, 0), {"primitive": {"shape": "box", "size": [1, 9, 1], "color": "#4A4370"},
                                           "collider": {"shape": "rectangle", "size": [1, 9]}}))
    for i in range(24):
        x = -7.0 + (i % 6) * 1.5
        y = 2.0 + (i // 6) * 1.7
        colour = COLOURS[i % len(COLOURS)]
        rb = {"type": "dynamic", "plane": "xy"}
        if i % 2 == 0:
            comps = {"primitive": {"shape": "box", "size": [1.0, 0.6, 1], "color": colour},
                     "collider": {"shape": "rectangle", "size": [1.0, 0.6]}, "rigidbody": rb}
        else:
            comps = {"primitive": {"shape": "sphere", "radius": 0.4, "color": colour},
                     "collider": {"shape": "circle", "radius": 0.4}, "rigidbody": rb}
        ents.append(entity(f"Piece {i + 1}", (x, y, 0), comps, z_turn(17 * i)))
    write_scene("Physics2D_Pile", ents)


def glow_sheet():
    size = 32
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    for y in range(size):
        for x in range(size):
            d = math.hypot(x + 0.5 - size / 2, y + 0.5 - size / 2) / (size / 2)
            a = max(0.0, 1.0 - d) ** 1.5
            img.putpixel((x, y), (255, 236, 160, int(round(255 * a))))
    return sheet("glow", img, 1, 1, [0.5, 0.5], 16, "linear")


def bleed_sheet():
    img = Image.new("RGBA", (16, 8), (0, 0, 0, 0))
    for y in range(1, 7):
        for x in range(1, 7):
            img.putpixel((x, y), (60, 220, 120, 255))  # frame 0: green, a clear 1-texel border
    for y in range(8):
        for x in range(8, 16):
            img.putpixel((x, y), (255, 0, 0, 255))     # frame 1: solid red, right next to it
    return sheet("bleed", img, 2, 1, [0.5, 0.5], 8, "nearest")


def sprite(frame, **extra):
    s = {"sheet": WALKER, "frame": frame, "billboard": "none", "cast_shadows": False}
    s.update(extra)
    return {"sprite": s}


def layers():
    glow = glow_sheet()
    ents = camera_and_sun(10, at=(0, 0, 20))
    # Three walkers at one depth, overlapping, listed back to front the "wrong" way: the layers decide.
    ents.append(entity("Front (layer 2)", (-3.2, -1.0, 0), sprite(0, layer=2, color="#FF7070")))
    ents.append(entity("Back (layer -1)", (-4.4, -1.0, 0), sprite(0, layer=-1, color="#70FF90")))
    ents.append(entity("Middle (layer 0)", (-3.8, -1.3, 0), sprite(0, layer=0, color="#70A0FF")))
    # Coins in one layer by order: the higher order is drawn later, so in front.
    for i, order in enumerate([3, 1, 2, 0]):
        ents.append(entity(f"Coin order {order}", (0.2 + i * 0.6, -1.0, 0), sprite(4, order=order, color=COLOURS[i])))
    # Soft-edged glows over them (layer 5), blending smoothly.
    ents.append(entity("Glow A", (-3.6, 0.4, 0), {"sprite": {"sheet": glow, "frame": 0, "billboard": "none", "cast_shadows": False, "blend": True, "layer": 5}}))
    ents.append(entity("Glow B", (1.1, 0.0, 0), {"sprite": {"sheet": glow, "frame": 0, "billboard": "none", "cast_shadows": False, "blend": True, "layer": 5}}))
    write_scene("Sprites2D_Layers", ents)
    # Lit against unlit under a dim sun (a separate scene: the light differs).
    dim = camera_and_sun(6, illuminance=6000, at=(0, 0, 20))
    dim.append(entity("Lit coin", (-1.2, -1.0, 0), sprite(4)))
    dim.append(entity("Unlit coin", (1.2, -1.0, 0), sprite(4, unlit=True)))
    write_scene("Sprites2D_Unlit", dim)


def ysort():
    ents = camera_and_sun(10, at=(0, 0, 20))
    # A crowd on a slope of heights, listed top to bottom so scene order would draw the lowest (nearest) first.
    k = 0
    for row in range(5):
        for col in range(4):
            y = 2.0 - row * 0.55
            x = -3.0 + col * 1.9 + (0.6 if row % 2 else 0.0)
            ents.append(entity(f"Walker {k}", (x, y, 0), sprite(k % 4, sort="y", color=COLOURS[(row + col) % len(COLOURS)])))
            k += 1
    write_scene("Sprites2D_YSort", ents)


def bleed():
    sh = bleed_sheet()
    ents = camera_and_sun(1.6, at=(0, 0, 20))
    ents.append(entity("Frame 0", (0, 0, 0), {"sprite": {"sheet": sh, "frame": 0, "billboard": "none", "cast_shadows": False, "unlit": True}}))
    write_scene("Sprites2D_Bleed", ents)


# ---- tilemaps (part 4) ----

def tiles_meta(name, img, columns, rows, pivot, ppm, solid=(), one_way=()):
    """A tile sheet under Assets/Tiles: the picture, its .noorsprite and a .noortileset."""
    folder = ROOT / "Assets" / "Tiles"
    folder.mkdir(parents=True, exist_ok=True)
    png = folder / f"{name}.png"
    img.save(png)
    meta(png, gid("tiles " + name + ".png"), "texture", {"kind": "color", "max_size": 4096, "mips": False, "normal_convention": "opengl"})
    sheet = {"noor_sprite": 1, "name": name, "texture": gid("tiles " + name + ".png"),
             "grid": {"columns": columns, "rows": rows, "margin": 0, "spacing": 0}, "pivot": pivot, "pixels_per_metre": ppm,
             "filter": "nearest", "animations": {}}
    sp = folder / f"{name}.noorsprite"
    sp.write_text(json.dumps(sheet, indent=2) + "\n", encoding="utf-8", newline="\n")
    meta(sp, gid("tiles " + name + ".noorsprite"), "sprite", {})
    ts = {"noor_tileset": 1, "name": name, "sheet": gid("tiles " + name + ".noorsprite"), "solid": list(solid), "one_way": list(one_way)}
    tp = folder / f"{name}.noortileset"
    tp.write_text(json.dumps(ts, indent=2) + "\n", encoding="utf-8", newline="\n")
    meta(tp, gid("tiles " + name + ".noortileset"), "tileset", {})
    return gid("tiles " + name + ".noortileset")


def square_tiles():
    # 0 grass on dirt, 1 dirt, 2 brick, 3 dark stone, 4 plank (one-way), 5 flowers, 6 crate, 7 sky-blue glass
    rnd = random.Random(7)
    img = Image.new("RGBA", (16 * 8, 16), (0, 0, 0, 0))
    px = img.load()

    def fill(t, colour, noise=10):
        for y in range(16):
            for x in range(16):
                n = rnd.randint(-noise, noise)
                px[t * 16 + x, y] = tuple(max(0, min(255, c + n)) for c in colour) + (255,)

    fill(0, (120, 82, 52)); fill(1, (120, 82, 52)); fill(3, (70, 70, 84), 8)
    for x in range(16):  # grass on tile 0
        for y in range(4 + (x * 7 % 3)):
            px[x, y] = (74 + rnd.randint(-10, 10), 168 + rnd.randint(-12, 12), 72, 255)
    for y in range(16):  # bricks on tile 2
        for x in range(16):
            mortar = y % 8 == 7 or (x + (4 if (y // 8) % 2 else 0)) % 8 == 7
            px[2 * 16 + x, y] = (196, 190, 176, 255) if mortar else (168 + rnd.randint(-12, 12), 64, 52, 255)
    for y in range(0, 5):  # plank on tile 4: a board along the top
        for x in range(16):
            px[4 * 16 + x, y] = (176, 128, 70, 255) if y not in (0, 4) else (110, 72, 36, 255)
    for x, h, col in [(3, 6, (240, 90, 120)), (8, 9, (250, 210, 80)), (12, 5, (150, 120, 250))]:  # flowers on tile 5
        for y in range(16 - h, 16):
            px[5 * 16 + x, y] = (60, 150, 60, 255)
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                px[5 * 16 + x + dx, 16 - h + dy] = col + (255,)
    d = ImageDraw.Draw(img)  # crate on tile 6
    d.rectangle([6 * 16, 0, 6 * 16 + 15, 15], fill=(150, 100, 54, 255), outline=(90, 56, 26, 255))
    d.line([6 * 16 + 1, 1, 6 * 16 + 14, 14], fill=(90, 56, 26, 255))
    d.rectangle([7 * 16, 0, 7 * 16 + 15, 15], fill=(140, 200, 240, 150), outline=(220, 240, 255, 255))  # glass
    return tiles_meta("square", img, 8, 1, [0.5, 0.5], 16, solid=(0, 1, 2, 3, 6), one_way=(4,))


def iso_tiles():
    # 32 x 32 frames, 16 pixels a metre: a 2 m x 1 m diamond (cell [2, 1]) in the bottom half; pivot at its middle.
    img = Image.new("RGBA", (32 * 3, 32), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def diamond(ox, cy, colour, outline):
        d.polygon([(ox + 0, cy), (ox + 16, cy - 8), (ox + 31, cy), (ox + 16, cy + 8)], fill=colour, outline=outline)

    diamond(0, 24, (96, 172, 84, 255), (70, 130, 60, 255))        # 0 grass
    diamond(32, 24, (214, 196, 140, 255), (176, 156, 104, 255))   # 1 sand
    # 2 a stone block one cell tall: the base diamond's side faces, then the top diamond 8 px higher... 16 px higher.
    d.polygon([(64, 24), (80, 32), (80, 16), (64, 8)], fill=(120, 120, 136, 255))      # left face
    d.polygon([(80, 32), (95, 24), (95, 8), (80, 16)], fill=(92, 92, 108, 255))        # right face
    diamond(64, 8, (168, 168, 184, 255), (120, 120, 136, 255))                         # top
    return tiles_meta("iso", img, 3, 1, [0.5, 0.25], 16)


def hex_tiles():
    # Pointy-top hexagons 16 px wide (1 m at 16 px/m), 18 px tall, in 16 x 18 frames; pivot at the middle.
    img = Image.new("RGBA", (16 * 3, 18), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for i, (fill, line) in enumerate([((96, 172, 84, 255), (60, 120, 54, 255)), ((214, 196, 140, 255), (170, 150, 100, 255)),
                                      ((80, 140, 220, 255), (50, 100, 180, 255))]):
        ox = i * 16
        d.polygon([(ox + 8, 0), (ox + 15, 4), (ox + 15, 13), (ox + 8, 17), (ox + 0, 13), (ox + 0, 4)], fill=fill, outline=line)
    return tiles_meta("hex", img, 3, 1, [0.5, 0.5], 16)


def tilemap_entity(name, tileset, rows, pos=(0, 0, 0), **extra):
    c = {"tileset": tileset, "rows": rows}
    c.update(extra)
    return entity(name, pos, {"tilemap": c})


def tilemaps():
    sq = square_tiles()
    rows = [
        ". . . . . . . . . . . . . . . . . . . .",
        ". . . . . . . . . . . . . . . . . . . .",
        ". . . . . . . . . . . . . . . . 2 2 . .",
        ". . . . . . . 4 4 4 4 . . . . . 2 2 . .",
        ". . . . . . . . . . . . . . . . 2 2 . .",
        ". . . . . 5 . . . . . . 6 . 5 . 2 2 . .",
        "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 3 3 0 0",
        "1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 3 3 1 1",
    ]
    ents = camera_and_sun(9, at=(9.5, 3.5, 20))
    ents.append(tilemap_entity("Level", sq, rows))
    ents.append(entity("Walker", (4, 1.5, 0.1), sprite(0)))
    write_scene("Tilemap_Square", ents)

    iso = iso_tiles()
    n = 8
    iso_rows = []
    for r in range(n):
        y = n - 1 - r
        row = []
        for x in range(n):
            if (x, y) in [(2, 2), (2, 3), (5, 1), (4, 5), (5, 5), (6, 5)]:
                row.append("2")
            elif x == 0 or y == 0:
                row.append("1")
            else:
                row.append("0")
        iso_rows.append(" ".join(row))
    ents = camera_and_sun(11, at=(0, 4, 20))
    ents.append(tilemap_entity("Iso", iso, iso_rows, layout="isometric", cell=[2, 1]))
    write_scene("Tilemap_Iso", ents)

    hx = hex_tiles()
    rnd = random.Random(3)
    hex_rows = []
    for r in range(9):
        hex_rows.append(" ".join(str(2 if (r in (3, 4) and 3 < c < 9) else rnd.choice([0, 0, 0, 1])) for c in range(12)))
    ents = camera_and_sun(9, at=(5.75, 3.0, 20))
    ents.append(tilemap_entity("Hexes", hx, hex_rows, layout="hexagonal"))
    write_scene("Tilemap_Hex", ents)

    rnd = random.Random(11)
    big = [" ".join(str(rnd.choice([0, 1, 2, 3, 5, 6, 7])) if rnd.random() < 0.7 else "." for _ in range(256)) for _ in range(256)]
    ents = camera_and_sun(270, at=(127.5, 127.5, 20))
    ents.append(tilemap_entity("Big", sq, big))
    write_scene("Tilemap_Big", ents)


if __name__ == "__main__":
    pile()
    layers()
    ysort()
    bleed()
    tilemaps()
    print("2D test scenes written to", ROOT / "Scenes")
