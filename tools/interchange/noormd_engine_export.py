"""Convert a NoorMD NoorForm tree pack (today's export) into the NoorEngine export layout.

NoorMD's "NoorEngine" export mode (docs/interchange/NOORMD_NOORENGINE_EXPORT.md) doesn't exist
yet. Until it does, this script produces the same layout from an ordinary NoorForm variety pack
(`Oak.manifest.json` + `Oak_0N_LOD0..3.glb` + `Oak_0N.noortree.json`), so NoorEngine has real
files to import and test against. It is a reference for the export mode too: every rule it applies
is a rule from the guide.

It is NOT part of the engine: NoorEngine reads only the Noor Interchange layout and never learns
NoorForm's own manifest. When NoorMD writes the layout itself, its output replaces this script's.

    python tools/interchange/noormd_engine_export.py <pack folder> <variant name> <out folder>
    e.g.  python tools/interchange/noormd_engine_export.py test_tree "Oak 01" tests/data/interchange/oak_01

Needs Pillow (JPEG re-encoding and smaller maps on far levels).
"""

import hashlib
import io
import json
import struct
import sys
import uuid
from pathlib import Path

from PIL import Image

NOORFORM_NAMESPACE = uuid.UUID("1f6b9f73-1acd-4a74-8358-4dba96ea8782")  # the guide, section 3
JPEG_QUALITY = 92
CAPSULE_HEIGHT = 2.5  # metres of trunk the collision capsule covers (the guide: the lowest 2-3 m)


# --- GLB in and out ------------------------------------------------------------------------------

def read_glb(path):
    data = Path(path).read_bytes()
    magic, version, _ = struct.unpack_from("<III", data, 0)
    assert magic == 0x46546C67 and version == 2, f"{path} is not a GLB 2.0 file"
    json_len, json_type = struct.unpack_from("<II", data, 12)
    assert json_type == 0x4E4F534A
    doc = json.loads(data[20:20 + json_len])
    off = 20 + json_len
    bin_len, bin_type = struct.unpack_from("<II", data, off)
    assert bin_type == 0x004E4942
    return doc, data[off + 8:off + 8 + bin_len]


def view_bytes(doc, blob, index):
    v = doc["bufferViews"][index]
    start = v.get("byteOffset", 0)
    return blob[start:start + v["byteLength"]]


def write_glb(path, doc, views):
    """views: the bytes of every bufferView, in order. The buffer is rebuilt from them."""
    blob = bytearray()
    for i, b in enumerate(views):
        while len(blob) % 4:
            blob.append(0)
        doc["bufferViews"][i]["byteOffset"] = len(blob)
        doc["bufferViews"][i]["byteLength"] = len(b)
        blob += b
    while len(blob) % 4:
        blob.append(0)
    doc["buffers"] = [{"byteLength": len(blob)}]
    text = json.dumps(doc, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    text += b" " * ((4 - len(text) % 4) % 4)
    out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(blob))
    out += struct.pack("<II", len(text), 0x4E4F534A) + text
    out += struct.pack("<II", len(blob), 0x004E4942) + bytes(blob)
    Path(path).write_bytes(out)


def accessor_vec3(doc, blob, index):
    a = doc["accessors"][index]
    assert a["componentType"] == 5126 and a["type"] == "VEC3"
    v = doc["bufferViews"][a["bufferView"]]
    stride = v.get("byteStride", 12)
    start = v.get("byteOffset", 0) + a.get("byteOffset", 0)
    return [struct.unpack_from("<3f", blob, start + i * stride) for i in range(a["count"])]


# --- The rules from the guide --------------------------------------------------------------------

def images_needing_alpha(doc):
    """Images used as the base colour of a MASK or BLEND material keep their alpha (PNG)."""
    keep = set()
    for m in doc.get("materials", []):
        if m.get("alphaMode", "OPAQUE") != "OPAQUE":
            t = m.get("pbrMetallicRoughness", {}).get("baseColorTexture")
            if t is not None:
                keep.add(doc["textures"][t["index"]]["source"])
    return keep


def normal_images(doc):
    found = set()
    for m in doc.get("materials", []):
        if "normalTexture" in m:
            found.add(doc["textures"][m["normalTexture"]["index"]]["source"])
    return found


def reencode(raw, keep_alpha, is_normal, scale):
    img = Image.open(io.BytesIO(raw))
    img.load()
    if scale < 1.0:
        size = (max(16, round(img.width * scale)), max(16, round(img.height * scale)))
        img = img.resize(size, Image.LANCZOS)
    out = io.BytesIO()
    if keep_alpha:
        img.convert("RGBA").save(out, "PNG", optimize=True)
        return out.getvalue(), "image/png"
    # Opaque maps: JPEG. A stray alpha channel (ORM maps have an unused one) is dropped only when
    # it really is unused, otherwise the image stays PNG.
    if img.mode in ("RGBA", "LA") and img.getchannel("A").getextrema()[0] < 255:
        img.save(out, "PNG", optimize=True)
        return out.getvalue(), "image/png"
    # 4:4:4 for normal maps (chroma subsampling bends normals), 4:2:0 for the rest.
    img.convert("RGB").save(out, "JPEG", quality=JPEG_QUALITY, subsampling=0 if is_normal else 2, optimize=True)
    return out.getvalue(), "image/jpeg"


PARTS = {"trunk": "trunk", "leaves": "leaves", "billboard": "leaves"}


def part_of(name):
    lower = name.lower()
    if lower.startswith("branch"):
        return "branch"
    return PARTS.get(lower)


def trunk_capsule(doc, blob):
    """A capsule around the lowest CAPSULE_HEIGHT metres of the trunk, from the Trunk mesh."""
    trunk = next(n for n in doc["nodes"] if n.get("name") == "Trunk" and "mesh" in n)
    assert "translation" not in trunk and "rotation" not in trunk and "scale" not in trunk and "matrix" not in trunk, \
        "the Trunk node is expected at the tree's origin"
    # The Trunk mesh also holds the roots (below ~1.5 m) and merged branches (above ~2.5 m), so the
    # capsule is fitted to the clean stretch between them, then stretched down to the ground.
    points = []
    for prim in doc["meshes"][trunk["mesh"]]["primitives"]:
        points += [p for p in accessor_vec3(doc, blob, prim["attributes"]["POSITION"]) if 1.5 <= p[1] <= 2.5]
    assert len(points) >= 6, "too few trunk vertices between 1.5 m and 2.5 m to fit a capsule"
    cx = (min(p[0] for p in points) + max(p[0] for p in points)) / 2
    cz = (min(p[2] for p in points) + max(p[2] for p in points)) / 2
    d = sorted(((p[0] - cx) ** 2 + (p[2] - cz) ** 2) ** 0.5 for p in points)
    radius = d[len(d) * 9 // 10]  # nearly all of the bark inside, without one stray bump deciding
    return round(cx, 4), round(cz, 4), round(radius, 4)


def convert_level(src, dst, base, level, info, kind):
    doc, blob = read_glb(src)
    views = [view_bytes(doc, blob, i) for i in range(len(doc["bufferViews"]))]

    alpha, normals = images_needing_alpha(doc), normal_images(doc)
    # Smaller maps on far levels: LOD1 half, LOD2 a quarter (the guide); the billboard (LOD3) half.
    scale = {0: 1.0, 1: 0.5, 2: 0.25}.get(level, 0.5)
    for i, image in enumerate(doc.get("images", [])):
        raw, mime = reencode(views[image["bufferView"]], i in alpha, i in normals, scale)
        views[image["bufferView"]] = raw
        image["mimeType"] = mime

    root_index = doc["scenes"][doc.get("scene", 0)]["nodes"][0]
    root = doc["nodes"][root_index]
    old = root.get("extras", {})
    max_distance = old.get("noor_visible_to", 0) or 0
    noor = {"v": 1, "lod": level, "max_distance": max_distance, "billboard": bool(old.get("noor_billboard", False)), "kind": kind}
    root["name"] = f"{base}_LOD{level}"
    root["extras"] = {"noor": noor}
    for scene in doc["scenes"]:
        scene["name"] = root["name"]
    for node in doc["nodes"]:
        part = part_of(node.get("name", ""))
        if part and "mesh" in node:
            node.setdefault("extras", {})["noor"] = {"v": 1, "part": part}

    capsule = None
    if level == 0:
        cx, cz, radius = trunk_capsule(doc, blob)
        capsule = {"radius": radius, "height": CAPSULE_HEIGHT}
        doc["nodes"].append({"name": "Trunk-colonly", "translation": [cx, CAPSULE_HEIGHT / 2, cz],
                             "extras": {"noor": {"v": 1, "collision": "capsule", "radius": radius, "height": CAPSULE_HEIGHT}}})
        root.setdefault("children", []).append(len(doc["nodes"]) - 1)

    doc["asset"]["generator"] = "NoorMD NoorForm (converted to the NoorEngine layout by NoorEngine's tools/interchange/noormd_engine_export.py)"
    write_glb(dst, doc, views)
    return noor, capsule


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    pack_dir, variant_name, out_dir = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
    pack_file = next(pack_dir.glob("*.manifest.json"))
    pack = json.loads(pack_file.read_text(encoding="utf-8-sig"))
    variants = pack["variants"]
    v = next(x for x in variants if x["name"] == variant_name)
    base = v["name"].replace(" ", "_")
    kind = {"tree": "vegetation.tree", "rock": "rock"}.get(v["kind"], "vegetation.plant")
    out_dir.mkdir(parents=True, exist_ok=True)

    # The recipe, renamed (same bytes: it already has version and seed).
    recipe_src = pack_dir / v["recipe"]
    recipe_dst = out_dir / f"{base}.recipe.json"
    recipe_dst.write_bytes(recipe_src.read_bytes())
    recipe = json.loads(recipe_src.read_text(encoding="utf-8-sig"))
    seed = int(recipe["seed"])

    files, lods, capsule = [], [], None
    for lod in v["lods"]:
        level = lod["level"]
        name = f"{base}.glb" if level == 0 else f"{base}.lod{level}.glb"
        noor, cap = convert_level(pack_dir / lod["file"], out_dir / name, base, level, lod, kind)
        capsule = capsule or cap
        entry = {"path": name, "role": "model" if level == 0 else "lod", "sha256": sha256(out_dir / name)}
        if level:
            entry["lod"] = level
        files.append(entry)
        item = {"level": level, "triangles": lod["triangles"]}
        if noor["max_distance"]:
            item["max_distance"] = noor["max_distance"]
        if noor["billboard"]:
            item["billboard"] = True
        lods.append(item)
    files.append({"path": recipe_dst.name, "role": "recipe"})

    wind = None
    if isinstance(v.get("wind"), dict) and "TEXCOORD_1" in v["wind"].get("channel", ""):
        wind = {"channels": {"bend": "TEXCOORD_1.x", "flutter": "TEXCOORD_1.y", "phase": "TEXCOORD_2.x", "sway": "TEXCOORD_2.y"}}

    asset_id = recipe.get("id") or str(uuid.uuid5(NOORFORM_NAMESPACE, f"noorform:{v['kind']}:{v['name']}:{seed}"))
    manifest = {
        "noor_interchange": "0.4",
        "id": asset_id,
        "name": base,
        "kind": kind,
        "generator": {"tool": "NoorForm", "version": "unknown",
                      "determinism": "same_platform"},
        "recipe": {"file": recipe_dst.name, "seed": seed, "sha256": sha256(recipe_dst)},
        "space": {"up": "+Y", "forward": "+Z", "handedness": "right", "units": "meters"},
        "bounds": {"min": v["bounds"]["min"], "max": v["bounds"]["max"]},
        "files": files,
        "lods": lods,
        "collision": "capsule" if capsule else "none",
        "family": {"name": pack["name"], "variant": v.get("variant", ""), "count": len(variants)},
        "tags": [v.get("species", "tree"), "broadleaf"] if v.get("species") == "oak" else [v.get("species", "tree")],
        "license": {"spdx": "MIT", "author": "Bad Ideas Works", "attribution": ""},
        "validation": {"gltf_validator": "not_run"},
        "noorform": {"species": v.get("species"), "height": v.get("heightMetres"), "texture": v.get("texture"),
                     "vertex_colors": "tint (COLOR_0 multiplies the base colour, as glTF says)",
                     "converted_from": f"{pack_file.name} + {', '.join(l['file'] for l in v['lods'])} "
                                       "(a NoorForm pack from before the NoorEngine export mode; its NoorMD version isn't recorded)"},
    }
    if wind:
        manifest["wind"] = wind
    (out_dir / f"{base}.noor.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")

    total = sum((out_dir / f["path"]).stat().st_size for f in files)
    print(f"{base}: id {asset_id}, {len(lods)} levels, {total / 1e6:.2f} MB")
    for f in files:
        print(f"  {f['path']:24} {(out_dir / f['path']).stat().st_size / 1e6:6.2f} MB")
    if capsule:
        print(f"  trunk capsule: radius {capsule['radius']} m, height {capsule['height']} m")
    return 0


if __name__ == "__main__":
    sys.exit(main())
