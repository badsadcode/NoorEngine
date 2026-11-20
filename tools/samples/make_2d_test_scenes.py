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
"""
import hashlib
import json
import math
import pathlib
import uuid

from PIL import Image

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


if __name__ == "__main__":
    pile()
    layers()
    ysort()
    bleed()
    print("2D test scenes written to", ROOT / "Scenes")
