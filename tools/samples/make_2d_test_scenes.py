"""Test scenes for 2D (the 2D foundations, ADR-040).

    python tools/samples/make_2d_test_scenes.py

Writes into tests/data/render_scenes/Scenes:
  Physics2D_Pile.noorscene   part 1: rectangles and circles locked to the XY plane, dropped onto a tilted floor
                             against a wall, seen by an orthographic camera (a golden four seconds in)
"""
import json
import math
import pathlib
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[2] / "tests" / "data" / "render_scenes"
NS = uuid.UUID("9a02d000-0000-4000-8000-0000000000d2")
COLOURS = ["#A07CFF", "#FFC857", "#5FD3A8", "#FF6B6B", "#6BB8FF", "#F2EEFF"]


def gid(name):
    return str(uuid.uuid5(NS, name))


def z_turn(degrees):
    h = math.radians(degrees) * 0.5
    return [0.0, 0.0, math.sin(h), math.cos(h)]


def entity(name, position, components, rotation=(0, 0, 0, 1)):
    return {"id": gid(name), "name": name, "layer": "default", "tags": [], "parent": None,
            "transform": {"position": list(position), "rotation": list(rotation), "scale": [1, 1, 1]},
            "components": components}


def pile():
    ents = [
        entity("Camera", (0, 3, 20), {"camera": {"projection": "orthographic", "height": 14, "near": 0.1, "far": 60, "ev100": 14}}),
        entity("Sun", (0, 0, 30), {"light": {"type": "directional", "color": "#FFFFFF", "illuminance": 90000, "shadows": False,
                                             "sky_color": "#B8B0FF", "sky_luminance": 3000}}),
        entity("Backdrop", (0, 0, -2), {"primitive": {"shape": "box", "size": [60, 40, 0.2], "color": "#141127", "roughness": 0.9}}),
        entity("Ground", (0, -2, 0), {"primitive": {"shape": "box", "size": [24, 1, 1], "color": "#4A4370"},
                                      "collider": {"shape": "rectangle", "size": [24, 1]}}, z_turn(-12)),
        entity("Wall", (8, 2, 0), {"primitive": {"shape": "box", "size": [1, 9, 1], "color": "#4A4370"},
                                   "collider": {"shape": "rectangle", "size": [1, 9]}}),
    ]
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
    s = {"noor_scene": 2, "id": gid("scene Physics2D_Pile"), "name": "Physics2D_Pile", "entities": ents}
    (ROOT / "Scenes" / "Physics2D_Pile.noorscene").write_text(json.dumps(s, indent=2) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    pile()
    print("2D test scenes written to", ROOT / "Scenes")
