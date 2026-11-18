"""Builds a forest test project: NoorForm oaks (tests/data/noorform_oak) on a ground plane.

Milestone 7, part 6 (instancing and levels of detail). Two uses:

    python tools/samples/build_forest.py tests/data/forest_project --trees 120 --area 260 --name Forest
        the committed test project: a golden render and the level / batching checks
    python tools/samples/build_forest.py <somewhere outside the repo> --trees 2000 --area 900 --name Forest2000
        the benchmark in docs/rendering/MEASUREMENTS.md (not committed: a 2,000-tree scene is several MB)

Trees are placed on a jittered grid (deterministic: --seed), turned and scaled a little, and written
the way the engine places a model (scene::instantiate_model): a root entity with a `model` component
and one entity per glTF node below it, so the scene loads exactly as if each oak had been added in
the editor. The project's .noormeta files are written too, so running it never changes the folder.
"""

import argparse
import hashlib
import json
import math
import random
import shutil
import struct
import uuid
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
OAK = REPO / "tests" / "data" / "noorform_oak"
PROJECT_NS = uuid.UUID("f0a5e57c-6e0d-4a8f-9a3e-2c7a1d000000")  # ids in this project: uuid5(PROJECT_NS, name)


def gid(name):
    return str(uuid.uuid5(PROJECT_NS, name))


def glb_nodes(path):
    data = path.read_bytes()
    json_len = struct.unpack_from("<I", data, 12)[0]
    doc = json.loads(data[20:20 + json_len])
    return doc["nodes"], doc["scenes"][doc.get("scene", 0)]["nodes"]


def quat_y(angle):
    return [0.0, math.sin(angle / 2), 0.0, math.cos(angle / 2)]


def entity(eid, name, parent, position, rotation, scale, components):
    return {"id": eid, "name": name, "layer": "default", "tags": [], "parent": parent,
            "transform": {"position": position, "rotation": rotation, "scale": scale}, "components": components}


def oak_entities(index, asset_id, nodes, roots, position, yaw, scale):
    """One placed oak, as scene::instantiate_model writes it."""
    root_id = gid(f"oak {index}")
    out = [entity(root_id, f"Oak {index:04d}", None, position, quat_y(yaw), [scale] * 3, {"model": {"asset": asset_id}})]
    parent_of = {}
    for i, n in enumerate(nodes):
        for c in n.get("children", []):
            parent_of[c] = i
    order = []
    def visit(i):
        order.append(i)
        for c in nodes[i].get("children", []):
            visit(c)
    for r in roots:
        visit(r)
    for i in order:
        n = nodes[i]
        noor = n.get("extras", {}).get("noor")
        comps = {}
        if noor and "collision" in noor:
            comps["collider"] = {"shape": noor["collision"], "asset": asset_id, "mesh_index": n.get("mesh", -1)}
            for k in ("radius", "height", "size"):
                if k in noor:
                    comps["collider"][k] = noor[k]
        elif "mesh" in n:
            comps["mesh"] = {"asset": asset_id, "mesh_index": n["mesh"]}
        if noor and "marker" in noor:
            comps["marker"] = noor
        elif noor and "collision" not in noor:
            comps["noor"] = noor
        parent = gid(f"oak {index} node {parent_of[i]}") if i in parent_of else root_id
        out.append(entity(gid(f"oak {index} node {i}"), n.get("name", f"node {i}"), parent,
                          n.get("translation", [0, 0, 0]), n.get("rotation", [0, 0, 0, 1]), n.get("scale", [1, 1, 1]), comps))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--trees", type=int, default=120)
    ap.add_argument("--area", type=float, default=260.0, help="side of the square the trees stand in (m)")
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--name", default="Forest")
    a = ap.parse_args()

    out = Path(a.out)
    (out / "Scenes").mkdir(parents=True, exist_ok=True)
    trees = out / "Assets" / "Trees"
    trees.mkdir(parents=True, exist_ok=True)
    for f in OAK.iterdir():
        if not (trees / f.name).exists() or (trees / f.name).read_bytes() != f.read_bytes():
            shutil.copyfile(f, trees / f.name)
    manifest = json.loads((trees / "Oak_01.noor.json").read_text(encoding="utf-8"))
    asset_id = manifest["id"]
    glb = trees / "Oak_01.glb"
    meta = {"guid": asset_id, "importer": "gltf", "interchange_manifest": "Oak_01.noor.json", "noor_meta": 1,
            "settings": {}, "source_sha256": hashlib.sha256(glb.read_bytes()).hexdigest()}
    (trees / "Oak_01.glb.noormeta").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8", newline="\n")
    project = out / f"{out.name}.noorproj" if not list(out.glob("*.noorproj")) else next(out.glob("*.noorproj"))
    if not project.exists():
        project = out / "Forest.noorproj"
        project.write_text(json.dumps({"noor_project": 1, "id": gid("project"), "name": "Forest", "engine": "0.0.1",
                                       "created": "2026-11-18T12:00:00Z"}, indent=2) + "\n", encoding="utf-8", newline="\n")

    nodes, roots = glb_nodes(glb)
    rng = random.Random(a.seed)
    side = max(1, round(math.sqrt(a.trees)))
    cell = a.area / side
    ents = [
        entity(gid("ground"), "Ground", None, [0, 0, 0], [0, 0, 0, 1], [1, 1, 1],
               {"primitive": {"shape": "plane", "size": [a.area * 3, 1, a.area * 3], "color": "#5A6B3A"}}),
        # A low afternoon sun from the left, so trunks and crowns cast long shadows.
        entity(gid("sun"), "Sun", None, [0, 30, 0], [-0.2241, -0.4830, -0.1294, 0.8365], [1, 1, 1],
               {"light": {"type": "directional", "color": "#FFF1DC", "illuminance": 90000, "shadow_distance": 120}}),
        # At the forest's edge, eye height, looking in (-Z) and slightly down.
        entity(gid("camera"), "Camera", None, [0, 1.7, a.area / 2 + 6], [-0.0131, 0, 0, 0.9999], [1, 1, 1],
               {"camera": {"fov": 60, "near": 0.1, "ev100": 14}}),
    ]
    placed = 0
    for gz in range(side):
        for gx in range(side):
            if placed >= a.trees:
                break
            x = -a.area / 2 + (gx + 0.5) * cell + rng.uniform(-0.35, 0.35) * cell
            z = -a.area / 2 + (gz + 0.5) * cell + rng.uniform(-0.35, 0.35) * cell
            if abs(x) < 6 and z > a.area / 2 - 14:
                rng.random(); rng.random()
                continue  # a clearing in front of the camera
            ents += oak_entities(placed, asset_id, nodes, roots, [round(x, 3), 0, round(z, 3)],
                                 round(rng.uniform(0, 2 * math.pi), 4), round(rng.uniform(0.85, 1.15), 3))
            placed += 1
    scene = {"noor_scene": 2, "id": gid(f"scene {a.name}"), "name": a.name, "entities": ents}
    path = out / "Scenes" / f"{a.name}.noorscene"
    path.write_text(json.dumps(scene, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"{path}: {placed} oaks, {len(ents)} entities, {path.stat().st_size / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
