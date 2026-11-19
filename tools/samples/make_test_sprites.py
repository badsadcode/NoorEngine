"""Test sprite sheet and scenes for sprites (Milestone 8B, part 5).

Writes into tests/data/render_scenes:
  Assets/Sprites/walker.png        a 128 x 64 pixel-art sheet: 4 x 2 frames of 32 x 32 (row 0 a figure walking,
                                   row 1 a coin turning), drawn here pixel by pixel so it's ours and reproducible
  Assets/Sprites/walker.noorsprite its grid, pivot (feet), 16 pixels a metre (frames 2 m tall), walk and spin
  Scenes/Sprites_Sun.noorscene     sprites standing in sunlight, casting shadows on the floor
  Scenes/Sprites_Shade.noorscene   a sprite half in a wall's shadow, half in the sun

Run: python tools/samples/make_test_sprites.py   (needs Pillow)
"""
import hashlib
import json
import pathlib

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2] / "tests" / "data" / "render_scenes"
SPRITES = ROOT / "Assets" / "Sprites"
SHEET_GUID = "5b417e00-0001-4000-8000-000000000001"
PNG_GUID = "5b417e00-0001-4000-8000-000000000002"

OUTLINE = (24, 20, 28, 255)
SKIN = (240, 196, 150, 255)
HAIR = (110, 60, 30, 255)
SHIRT = (60, 130, 220, 255)
TROUSERS = (50, 50, 70, 255)
SHOE = (90, 50, 30, 255)
GOLD = (250, 200, 40, 255)
GOLD_DARK = (180, 120, 20, 255)


def rect(img, ox, oy, x0, y0, x1, y1, color):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if 0 <= x < 32 and 0 <= y < 32:
                img.putpixel((ox + x, oy + y), color)


def outline(img, ox, oy):
    """A dark outline round everything drawn in the frame (pixel-art style)."""
    frame = [[img.getpixel((ox + x, oy + y))[3] > 0 for x in range(32)] for y in range(32)]
    for y in range(32):
        for x in range(32):
            if frame[y][x]:
                continue
            if any(0 <= x + dx < 32 and 0 <= y + dy < 32 and frame[y + dy][x + dx] for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                img.putpixel((ox + x, oy + y), OUTLINE)


def walker(img, ox, oy, step):
    # Legs: four poses of a walk (stride, pass, other stride, pass); the body bobs on the passing frames.
    bob = 1 if step % 2 == 1 else 0
    left, right = [(-3, 3), (0, 0), (3, -3), (0, 0)][step]
    for leg_x, swing in ((13, left), (17, right)):
        rect(img, ox, oy, leg_x + swing // 2, 22, leg_x + 1 + swing // 2, 28, TROUSERS)
        rect(img, ox, oy, leg_x + swing, 29, leg_x + 2 + swing, 30, SHOE)
    rect(img, ox, oy, 12, 13 + bob, 19, 22, SHIRT)                       # body
    arm = [2, 0, -2, 0][step]
    rect(img, ox, oy, 10, 14 + bob + max(0, arm), 11, 20 + bob + max(0, arm), SKIN)   # arms swing against the legs
    rect(img, ox, oy, 20, 14 + bob + max(0, -arm), 21, 20 + bob + max(0, -arm), SKIN)
    rect(img, ox, oy, 12, 5 + bob, 19, 12 + bob, SKIN)                   # head
    rect(img, ox, oy, 12, 4 + bob, 19, 6 + bob, HAIR)
    rect(img, ox, oy, 17, 8 + bob, 17, 9 + bob, OUTLINE)                 # an eye (it looks to the right)
    outline(img, ox, oy)


def coin(img, ox, oy, turn):
    half = [7, 5, 2, 5][turn]  # half-width as it turns
    for y in range(8, 25):
        dy = (y - 16) / 8.5
        w = int(half * (1 - dy * dy) ** 0.5 + 0.5)
        for x in range(16 - w, 16 + w):
            img.putpixel((ox + x, oy + y), GOLD if x < 16 + w - 2 or w < 2 else GOLD_DARK)
    outline(img, ox, oy)


def meta(path, guid, importer, settings):
    sha = hashlib.sha256(path.read_bytes()).hexdigest()
    m = {"guid": guid, "importer": importer, "noor_meta": 1, "settings": settings, "source_sha256": sha}
    path.with_name(path.name + ".noormeta").write_text(json.dumps(m, indent=2) + "\n", newline="\n")


def entity(idx, name, position, components, rotation=(0, 0, 0, 1), scale=(1, 1, 1), scene="00"):
    return {"id": f"5b417e00-00{scene}-4000-8000-{idx:012d}", "name": name, "layer": "default", "tags": [], "parent": None,
            "transform": {"position": list(position), "rotation": list(rotation), "scale": list(scale)}, "components": components}


def sprite(**fields):
    return {"sprite": {"sheet": SHEET_GUID, **fields}}


def sun_rotation(travel):
    """The quaternion turning a light's forward (-Z) to the way its light travels."""
    import math
    l = math.sqrt(sum(c * c for c in travel))
    d = [c / l for c in travel]
    axis = (0 * d[2] - (-1) * d[1], (-1) * d[0] - 0 * d[2], 0)  # (0, 0, -1) x d
    s = math.sqrt(axis[0] ** 2 + axis[1] ** 2)
    angle = math.acos(max(-1.0, min(1.0, -d[2])))
    h = math.sin(angle / 2)
    return (round(axis[0] / s * h, 9), round(axis[1] / s * h, 9), 0.0, round(math.cos(angle / 2), 9))


def common(scene, camera_pos, camera_rot, sun_travel=(-0.3535533, -0.7071068, -0.6123725)):
    return [
        entity(1, "Floor", (0, 0, 0), {"primitive": {"shape": "plane", "size": [30, 1, 30], "color": "#8FA070"}}, scene=scene),
        entity(2, "Camera", camera_pos, {"camera": {"fov": 55, "near": 0.05, "ev100": 15.0, "tonemapper": "neutral"}}, rotation=camera_rot,
               scene=scene),
        # By default the sun is behind the camera, to its right, 45 degrees up: long shadows away from the camera.
        entity(3, "Sun", (0, 5, 0), {"light": {"type": "directional", "illuminance": 100000}}, rotation=sun_rotation(sun_travel), scene=scene),
        entity(4, "Sky", (0, 0, 0), {"environment": {"sky": "atmosphere", "turbidity": 2, "ground_albedo": 0.3}}, scene=scene),
    ]


def write_scene(name, scene_id, entities):
    s = {"noor_scene": 2, "id": f"5b417e00-00{scene_id}-4000-8000-000000000000", "name": name, "entities": entities}
    (ROOT / "Scenes" / f"{name}.noorscene").write_text(json.dumps(s, indent=2) + "\n", newline="\n")


def main():
    SPRITES.mkdir(parents=True, exist_ok=True)
    img = Image.new("RGBA", (128, 64), (0, 0, 0, 0))
    for f in range(4):
        walker(img, f * 32, 0, f)
        coin(img, f * 32, 32, f)
    png = SPRITES / "walker.png"
    img.save(png, optimize=False)
    meta(png, PNG_GUID, "texture", {"kind": "color", "max_size": 4096, "mips": True, "normal_convention": "opengl"})

    sheet = {"noor_sprite": 1, "name": "Walker", "texture": PNG_GUID, "grid": {"columns": 4, "rows": 2}, "margin": 0, "spacing": 0,
             "pivot": [0.5, 1.0 / 32.0], "pixels_per_metre": 16, "filter": "nearest",
             "animations": {"walk": {"frames": {"from": 0, "to": 3}, "fps": 6, "loop": True},
                            "spin": {"frames": {"from": 4, "to": 7}, "fps": 8, "loop": True}}}
    noorsprite = SPRITES / "walker.noorsprite"
    noorsprite.write_text(json.dumps(sheet, indent=2) + "\n", newline="\n")
    meta(noorsprite, SHEET_GUID, "sprite", {})

    # In the sun: four walkers (each frame of the walk), one tinted, one flipped, a coin facing the camera fully,
    # and a walker animating; all casting shadows on the floor.
    camera_rot = (-0.0871557, 0.0, 0.0, 0.9961947)  # 10 degrees down
    sun = common("31", (0, 2.2, 7.5), camera_rot)
    for i in range(4):
        sun.append(entity(10 + i, f"Walker {i}", (-3 + i * 1.6, 0, 0), sprite(frame=i), scene="31"))
    sun.append(entity(20, "Tinted", (-2.2, 0, 2.2), sprite(frame=1, color="#FF9090"), scene="31"))
    sun.append(entity(21, "Flipped", (2.2, 0, 2.2), sprite(frame=0, flip_x=True), scene="31"))
    sun.append(entity(22, "Coin", (1.0, 1.3, 3.2), sprite(frame=4, billboard="camera"), scale=(0.5, 0.5, 0.5), scene="31"))
    sun.append(entity(23, "Animated", (0, 0, -2.5), sprite(animation="walk"), scene="31"))
    write_scene("Sprites_Sun", "31", sun)

    # In the shade: a low sun from the right; a wall on the right throws its shadow left across the walkers. The left
    # one stands in sun, the middle one has its legs in the shade (the wall's top edge crosses it), the right one is in
    # full shade. A coin with its own rotation ("none") hangs in the sun, casting no shadow.
    shade = common("32", (0, 2.0, 7.0), camera_rot, sun_travel=(-0.8, -0.45, -0.4))
    shade.append(entity(10, "Wall", (2.4, 1.5, 0.0), {"primitive": {"shape": "box", "size": [0.4, 3, 5], "color": "#B0A090"}}, scene="32"))
    shade.append(entity(11, "In sun", (-3.4, 0, 0), sprite(frame=0), scene="32"))
    shade.append(entity(12, "Legs in shade", (-1.2, 0, 0), sprite(frame=2), scene="32"))
    shade.append(entity(13, "In shade", (0.9, 0, 0), sprite(frame=1, flip_x=True), scene="32"))
    shade.append(entity(14, "Coin", (-2.4, 2.6, -1.5), sprite(frame=4, billboard="none", cast_shadows=False), scene="32"))
    write_scene("Sprites_Shade", "32", shade)
    print("sprite test data written")


if __name__ == "__main__":
    main()
