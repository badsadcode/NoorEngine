"""Alden Wayfarer, NoorEngine's mascot, as a skinned character for the 8C sample (Milestone 8C, part 8).

    python tools/samples/make_alden.py

His look is the concept sheet's (devlog #015): a blocky low-poly explorer with short brown hair and a beard, a teal
tunic with rolled sleeves, a brown belt with a gold square buckle, dark brown trousers and boots, a big brown backpack
with a rolled bedroll on top, and a wooden walking staff. Built here, so he's ours (MIT, like the repository).

He stands on Ward's skeleton (make_test_characters.py: 17 joints, Rigify-style names, a T-pose), so every motion made
for Ward is his too. Each body part is a box riding on one bone (blocky, as on the sheet), with flat faces.

Writes samples/Hillside/Assets/Characters/Alden/:
  Alden.glb   Alden with his clips: Idle, Walk, Run and Raise (Ward's), and, for root motion, WalkForward (1.4 m/s)
              and RunForward (4 m/s: Run's legs and arms with the hips carried 2.4 m each 0.6 s cycle).
  Staff.glb   his staff: a wooden pole with a knob, its origin at his grip (a prop for a socket on his right hand).
  Alden.nooranim  his controller: one "Move" state blending Idle, WalkForward and RunForward by "speed" (0, 1.4 and
              4 m/s: the speeds his clips carry him at, so with root motion the speed parameter is how fast he goes).
"""
import json
import math
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import make_test_characters as mtc  # noqa: E402

ROOT = mtc.ROOT
OUT = ROOT / 'samples' / 'Hillside' / 'Assets' / 'Characters' / 'Alden'

SKIN = (0.80, 0.58, 0.42)
HAIR = (0.32, 0.20, 0.11)
TEAL = (0.13, 0.48, 0.47)
TEAL_ROLL = (0.20, 0.58, 0.55)
BELT = (0.30, 0.18, 0.09)
GOLD = (0.86, 0.66, 0.22)
TROUSERS = (0.24, 0.16, 0.10)
BOOTS = (0.14, 0.09, 0.05)
PACK = (0.44, 0.28, 0.14)
BEDROLL = (0.55, 0.47, 0.30)
WOOD = (0.42, 0.27, 0.13)

# (bone, (x0, y0, z0), (x1, y1, z1), colour): boxes in model space at rest (the T-pose), each riding on its bone.
PARTS = [
    # Legs: trousers, boots
    ('DEF-thigh.L', (0.03, 0.48, -0.07), (0.17, 0.95, 0.07), TROUSERS),
    ('DEF-thigh.R', (-0.17, 0.48, -0.07), (-0.03, 0.95, 0.07), TROUSERS),
    ('DEF-shin.L', (0.035, 0.28, -0.065), (0.165, 0.52, 0.065), TROUSERS),
    ('DEF-shin.R', (-0.165, 0.28, -0.065), (-0.035, 0.52, 0.065), TROUSERS),
    ('DEF-shin.L', (0.03, 0.08, -0.07), (0.17, 0.32, 0.07), BOOTS),
    ('DEF-shin.R', (-0.17, 0.08, -0.07), (-0.03, 0.32, 0.07), BOOTS),
    ('DEF-foot.L', (0.035, 0.0, -0.07), (0.165, 0.11, 0.19), BOOTS),
    ('DEF-foot.R', (-0.165, 0.0, -0.07), (-0.035, 0.11, 0.19), BOOTS),
    # Hips: trousers, the belt and its gold buckle
    ('DEF-hips', (-0.18, 0.86, -0.11), (0.18, 1.02, 0.11), TROUSERS),
    ('DEF-hips', (-0.19, 1.00, -0.12), (0.19, 1.08, 0.12), BELT),
    ('DEF-hips', (-0.04, 1.005, 0.12), (0.04, 1.075, 0.14), GOLD),
    # The tunic
    ('DEF-spine', (-0.18, 1.06, -0.115), (0.18, 1.31, 0.115), TEAL),
    ('DEF-chest', (-0.20, 1.29, -0.125), (0.20, 1.53, 0.125), TEAL),
    # The backpack, its bedroll rolled on top
    ('DEF-chest', (-0.17, 1.02, -0.33), (0.17, 1.47, -0.12), PACK),
    ('DEF-chest', (-0.22, 1.47, -0.29), (0.22, 1.58, -0.17), BEDROLL),
    # Neck and head: skin, hair, beard
    ('DEF-neck', (-0.05, 1.49, -0.05), (0.05, 1.60, 0.05), SKIN),
    ('DEF-head', (-0.11, 1.58, -0.115), (0.11, 1.82, 0.115), SKIN),
    ('DEF-head', (-0.12, 1.78, -0.125), (0.12, 1.87, 0.12), HAIR),
    ('DEF-head', (-0.12, 1.62, -0.125), (0.12, 1.80, -0.06), HAIR),
    ('DEF-head', (-0.105, 1.57, 0.06), (0.105, 1.68, 0.135), HAIR),
    # Arms (the T-pose: along +-X): teal sleeves rolled at the elbow, bare forearms, hands
    ('DEF-upper_arm.L', (0.19, 1.395, -0.055), (0.42, 1.505, 0.055), TEAL),
    ('DEF-upper_arm.L', (0.40, 1.385, -0.065), (0.48, 1.515, 0.065), TEAL_ROLL),
    ('DEF-forearm.L', (0.47, 1.405, -0.045), (0.75, 1.495, 0.045), SKIN),
    ('DEF-hand.L', (0.74, 1.41, -0.035), (0.87, 1.49, 0.045), SKIN),
    ('DEF-upper_arm.R', (-0.42, 1.395, -0.055), (-0.19, 1.505, 0.055), TEAL),
    ('DEF-upper_arm.R', (-0.48, 1.385, -0.065), (-0.40, 1.515, 0.065), TEAL_ROLL),
    ('DEF-forearm.R', (-0.75, 1.405, -0.045), (-0.47, 1.495, 0.045), SKIN),
    ('DEF-hand.R', (-0.87, 1.41, -0.035), (-0.74, 1.49, 0.045), SKIN),
]

FACES = [((1, 0, 0), [(1, 0, 0), (1, 1, 0), (1, 1, 1), (1, 0, 1)]), ((-1, 0, 0), [(0, 0, 1), (0, 1, 1), (0, 1, 0), (0, 0, 0)]),
         ((0, 1, 0), [(0, 1, 0), (0, 1, 1), (1, 1, 1), (1, 1, 0)]), ((0, -1, 0), [(0, 0, 0), (1, 0, 0), (1, 0, 1), (0, 0, 1)]),
         ((0, 0, 1), [(0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)]), ((0, 0, -1), [(1, 0, 0), (0, 0, 0), (0, 1, 0), (1, 1, 0)])]


def box(lo, hi, colour, joint, out):
    positions, normals, colours, joints, weights, indices = out
    for n, corners in FACES:
        base = len(positions)
        for c in corners:
            positions.append(tuple(hi[i] if c[i] else lo[i] for i in range(3)))
            normals.append(n)
            colours.append(colour)
            joints.append((joint, 0, 0, 0))
            weights.append((1.0, 0.0, 0.0, 0.0))
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]


def alden_mesh():
    index = {b[0]: i for i, b in enumerate(mtc.BONES)}
    out = ([], [], [], [], [], [])
    for bone, lo, hi, colour in PARTS:
        box(lo, hi, colour, index[bone], out)
    return out


def alden_clips():
    keep = {name: chans for name, chans in mtc.clips()}
    run_t = [0, 0.15, 0.3, 0.45, 0.6]
    run_forward = [c for c in keep['Run'] if not (c[0] == 'DEF-hips' and c[1] == 'translation')]
    run_forward.append(('DEF-hips', 'translation', 'LINEAR', run_t,
                        [[0, y, z] for y, z in zip((0.92, 0.89, 0.92, 0.89, 0.92), (0, 0.6, 1.2, 1.8, 2.4))]))
    return [(n, keep[n]) for n in ('Idle', 'Walk', 'WalkForward', 'Run', 'Raise')] + [('RunForward', run_forward)]


def write_staff(path):
    """An octagonal wooden pole, 1.65 m, its origin at the grip (0.55 m below the top), with a knob on top; +Y up."""
    positions, normals, colours, indices = [], [], [], []
    sides = 8

    def prism(y0, y1, r0, r1):
        for s in range(sides):
            a0, a1 = 2 * math.pi * s / sides, 2 * math.pi * (s + 1) / sides
            n = (math.cos((a0 + a1) / 2), 0, math.sin((a0 + a1) / 2))
            base = len(positions)
            for (a, y, r) in ((a0, y0, r0), (a1, y0, r0), (a1, y1, r1), (a0, y1, r1)):
                positions.append((r * math.cos(a), y, r * math.sin(a)))
                normals.append(n)
                colours.append(WOOD)
            indices.extend([base, base + 2, base + 1, base, base + 3, base + 2])
        for y, r, up in ((y0, r0, -1), (y1, r1, 1)):  # caps
            c = len(positions)
            positions.append((0, y, 0))
            normals.append((0, up, 0))
            colours.append(WOOD)
            for s in range(sides):
                a = 2 * math.pi * s / sides
                positions.append((r * math.cos(a), y, r * math.sin(a)))
                normals.append((0, up, 0))
                colours.append(WOOD)
            for s in range(sides):
                i0, i1 = c + 1 + s, c + 1 + (s + 1) % sides
                indices.extend([c, i1, i0] if up > 0 else [c, i0, i1])

    prism(-1.10, 0.48, 0.022, 0.026)
    prism(0.48, 0.58, 0.04, 0.045)  # the knob
    blob = b''.join(struct.pack('<3f', *p) for p in positions)
    nrm = b''.join(struct.pack('<3f', *p) for p in normals)
    col = b''.join(struct.pack('<3f', *p) for p in colours)
    idx = b''.join(struct.pack('<I', i) for i in indices)
    pad = lambda b: b + b'\0' * (-len(b) % 4)  # noqa: E731
    chunks = [pad(blob), pad(nrm), pad(col), pad(idx)]
    offsets = [sum(len(c) for c in chunks[:i]) for i in range(4)]
    n = len(positions)
    lo = [min(p[i] for p in positions) for i in range(3)]
    hi = [max(p[i] for p in positions) for i in range(3)]
    gltf = {'asset': {'version': '2.0', 'generator': 'NoorEngine tools/samples/make_alden.py'}, 'scene': 0, 'scenes': [{'nodes': [0]}],
            'nodes': [{'name': 'Staff', 'mesh': 0}],
            'meshes': [{'name': 'Staff', 'primitives': [{'attributes': {'POSITION': 0, 'NORMAL': 1, 'COLOR_0': 2}, 'indices': 3, 'material': 0}]}],
            'materials': [{'name': 'Wood', 'pbrMetallicRoughness': {'baseColorFactor': [1, 1, 1, 1], 'metallicFactor': 0, 'roughnessFactor': 0.8}}],
            'accessors': [{'bufferView': 0, 'componentType': 5126, 'count': n, 'type': 'VEC3', 'min': lo, 'max': hi},
                          {'bufferView': 1, 'componentType': 5126, 'count': n, 'type': 'VEC3'},
                          {'bufferView': 2, 'componentType': 5126, 'count': n, 'type': 'VEC3'},
                          {'bufferView': 3, 'componentType': 5125, 'count': len(indices), 'type': 'SCALAR'}],
            'bufferViews': [{'buffer': 0, 'byteOffset': offsets[i], 'byteLength': len(chunks[i]), 'target': 34963 if i == 3 else 34962} for i in range(4)],
            'buffers': [{'byteLength': sum(len(c) for c in chunks)}]}
    binary = b''.join(chunks)
    js = json.dumps(gltf, separators=(',', ':')).encode()
    js += b' ' * (-len(js) % 4)
    path.write_bytes(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binary)) + struct.pack('<II', len(js), 0x4E4F534A) + js +
                     struct.pack('<II', len(binary), 0x004E4942) + binary)
    print('wrote', path.relative_to(ROOT), f'({n} vertices, {len(indices) // 3} triangles)')


CONTROLLER = {
    "noor_anim": 1,
    "parameters": {"speed": {"type": "float", "default": 0}},
    "layers": [{"name": "Base", "entry": "Move",
                "states": [{"name": "Move", "blend": {"x": "speed", "samples": [
                    {"clip": "Idle", "x": 0}, {"clip": "WalkForward", "x": 1.4}, {"clip": "RunForward", "x": 4}]}}]}],
}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'Alden.nooranim').write_text(json.dumps(CONTROLLER, indent=2) + chr(10), encoding='utf-8')
    rig = dict(mtc.WARD, name='Alden')
    mtc.write_glb(OUT / 'Alden.glb', posed=False, animated=True, rig=rig, mesh=alden_mesh(), clip_list=alden_clips())
    write_staff(OUT / 'Staff.glb')


if __name__ == '__main__':
    main()
