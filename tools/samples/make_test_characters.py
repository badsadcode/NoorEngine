"""Test characters for skeletal animation (Milestone 8C), made here so they're ours (MIT, like the repository).

    python tools/samples/make_test_characters.py

Writes into tests/data/animation/Ward/ (and copies into tests/data/render_scenes/Assets/Characters/ for goldens):

  Ward_Bind.glb   "Ward", a smoothly skinned humanoid, 1.8 m: 17 joints with Rigify-style deform names (DEF-spine,
                  DEF-upper_arm.L ...), bound in a T-pose. The file's own pose is the bind pose, so it draws as bound.
  Ward_Posed.glb  the same mesh and skin, but its joints' transforms are posed (arms down, the left elbow and right knee
                  bent, the head turned): before any animation exists, drawing it proves the whole skinning path.

Every body part is a tube along its bone; vertices near a joint blend between the two bones that meet there, so elbows
and knees bend smoothly. Colours: a blue-grey tunic, light grey limbs, a warm head (vertex colours).
"""
import json
import math
import pathlib
import shutil
import struct

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / 'tests' / 'data' / 'animation' / 'Ward'
SCENE_ASSETS = ROOT / 'tests' / 'data' / 'render_scenes' / 'Assets' / 'Characters'

# name, parent, head (model space, T-pose), radius at head, radius at tail, colour
TUNIC = (0.25, 0.35, 0.55)
LIMB = (0.72, 0.72, 0.75)
SKIN = (0.85, 0.62, 0.48)
BONES = [
    ('DEF-hips', None, (0, 0.95, 0), 0.17, 0.16, TUNIC),
    ('DEF-spine', 'DEF-hips', (0, 1.10, 0), 0.16, 0.18, TUNIC),
    ('DEF-chest', 'DEF-spine', (0, 1.30, 0), 0.19, 0.16, TUNIC),
    ('DEF-neck', 'DEF-chest', (0, 1.50, 0), 0.06, 0.06, SKIN),
    ('DEF-head', 'DEF-neck', (0, 1.58, 0), 0.11, 0.09, SKIN),
    ('DEF-upper_arm.L', 'DEF-chest', (0.20, 1.45, 0), 0.055, 0.05, LIMB),
    ('DEF-forearm.L', 'DEF-upper_arm.L', (0.48, 1.45, 0), 0.05, 0.04, LIMB),
    ('DEF-hand.L', 'DEF-forearm.L', (0.74, 1.45, 0), 0.04, 0.035, SKIN),
    ('DEF-upper_arm.R', 'DEF-chest', (-0.20, 1.45, 0), 0.055, 0.05, LIMB),
    ('DEF-forearm.R', 'DEF-upper_arm.R', (-0.48, 1.45, 0), 0.05, 0.04, LIMB),
    ('DEF-hand.R', 'DEF-forearm.R', (-0.74, 1.45, 0), 0.04, 0.035, SKIN),
    ('DEF-thigh.L', 'DEF-hips', (0.10, 0.92, 0), 0.08, 0.06, LIMB),
    ('DEF-shin.L', 'DEF-thigh.L', (0.10, 0.50, 0), 0.06, 0.045, LIMB),
    ('DEF-foot.L', 'DEF-shin.L', (0.10, 0.08, 0), 0.045, 0.04, TUNIC),
    ('DEF-thigh.R', 'DEF-hips', (-0.10, 0.92, 0), 0.08, 0.06, LIMB),
    ('DEF-shin.R', 'DEF-thigh.R', (-0.10, 0.50, 0), 0.06, 0.045, LIMB),
    ('DEF-foot.R', 'DEF-shin.R', (-0.10, 0.08, 0), 0.045, 0.04, TUNIC),
]
# Where each bone ends (its tail): the first child's head, or these for the ends of chains.
TAILS = {'DEF-head': (0, 1.80, 0), 'DEF-hand.L': (0.86, 1.45, 0), 'DEF-hand.R': (-0.86, 1.45, 0),
         'DEF-foot.L': (0.10, 0.02, 0.16), 'DEF-foot.R': (-0.10, 0.02, 0.16)}
# Ward_Posed: local rotations (axis, degrees) on top of the bind pose.
POSE = {'DEF-upper_arm.L': ((0, 0, 1), -70), 'DEF-upper_arm.R': ((0, 0, 1), 70), 'DEF-forearm.L': ((0, 1, 0), 50),
        'DEF-thigh.R': ((1, 0, 0), -35), 'DEF-shin.R': ((1, 0, 0), 60), 'DEF-head': ((0, 1, 0), 25)}

SIDES = 12
RINGS = 6


def sub(a, b): return tuple(x - y for x, y in zip(a, b))
def add(a, b): return tuple(x + y for x, y in zip(a, b))
def mul(a, k): return tuple(x * k for x in a)
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def norm(a):
    l = math.sqrt(dot(a, a))
    return mul(a, 1 / l) if l > 1e-12 else a


def smoothstep(e0, e1, x):
    t = min(1.0, max(0.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def build_mesh():
    index = {b[0]: i for i, b in enumerate(BONES)}
    children = {}
    for b in BONES:
        if b[1]:
            children.setdefault(b[1], []).append(b[0])
    positions, normals, colours, joints, weights, indices = [], [], [], [], [], []
    for bi, (name, parent, head, r0, r1, colour) in enumerate(BONES):
        tail = TAILS.get(name) or [h for n, _, h, *_ in BONES if n == children[name][0]][0]
        axis = sub(tail, head)
        length = math.sqrt(dot(axis, axis))
        d = norm(axis)
        side = norm(cross(d, (0, 0, 1) if abs(d[2]) < 0.9 else (1, 0, 0)))
        up = cross(side, d)
        child = index[children[name][0]] if name in children and len(children[name]) == 1 else None
        base = len(positions)
        for ring in range(RINGS + 1):
            t = ring / RINGS
            r = r0 + (r1 - r0) * t
            centre = add(head, mul(d, length * t))
            # Blend towards the parent near the head and towards the (single) child near the tail: smooth bends.
            w_parent = 0.5 * (1 - smoothstep(0.0, 0.35, t)) if parent else 0.0
            w_child = 0.5 * smoothstep(0.65, 1.0, t) if child is not None else 0.0
            for s in range(SIDES):
                a = 2 * math.pi * s / SIDES
                n = add(mul(side, math.cos(a)), mul(up, math.sin(a)))
                positions.append(add(centre, mul(n, r)))
                normals.append(n)
                colours.append(colour)
                infl = [(1.0 - w_parent - w_child, bi)]
                if w_parent > 0:
                    infl.append((w_parent, index[parent]))
                if w_child > 0:
                    infl.append((w_child, child))
                infl += [(0.0, 0)] * (4 - len(infl))
                joints.append(tuple(j for _, j in infl))
                weights.append(tuple(w for w, _ in infl))
        for ring in range(RINGS):
            for s in range(SIDES):
                a0 = base + ring * SIDES + s
                a1 = base + ring * SIDES + (s + 1) % SIDES
                b0, b1 = a0 + SIDES, a1 + SIDES
                indices += [a0, b0, a1, a1, b0, b1]
        # Caps: a centre vertex at each end, all on this bone.
        for end, centre_t in ((0, 0.0), (RINGS, 1.0)):
            c = len(positions)
            positions.append(add(head, mul(d, length * centre_t)))
            normals.append(mul(d, -1 if end == 0 else 1))
            colours.append(colour)
            joints.append((bi, 0, 0, 0))
            weights.append((1.0, 0.0, 0.0, 0.0))
            for s in range(SIDES):
                v0 = base + end * SIDES + s
                v1 = base + end * SIDES + (s + 1) % SIDES
                indices += [c, v1, v0] if end == 0 else [c, v0, v1]
    return positions, normals, colours, joints, weights, indices


def quat(axis, degrees):
    a = math.radians(degrees) / 2
    x, y, z = norm(axis)
    return [x * math.sin(a), y * math.sin(a), z * math.sin(a), math.cos(a)]


def write_glb(path, posed):
    positions, normals, colours, joints, weights, indices = build_mesh()
    blobs = []

    def add_blob(data):
        while sum(len(b) for b in blobs) % 4:
            blobs.append(b'\0')
        offset = sum(len(b) for b in blobs)
        blobs.append(data)
        return offset

    views, accessors = [], []

    def accessor(data, fmt, count, ctype, atype, target=None, minmax=None):
        off = add_blob(data)
        view = {'buffer': 0, 'byteOffset': off, 'byteLength': len(data)}
        if target:
            view['target'] = target
        views.append(view)
        acc = {'bufferView': len(views) - 1, 'componentType': ctype, 'count': count, 'type': atype}
        if minmax:
            acc['min'], acc['max'] = minmax
        accessors.append(acc)
        return len(accessors) - 1

    n = len(positions)
    lo = [min(p[i] for p in positions) for i in range(3)]
    hi = [max(p[i] for p in positions) for i in range(3)]
    a_pos = accessor(b''.join(struct.pack('<3f', *p) for p in positions), '', n, 5126, 'VEC3', 34962, (lo, hi))
    a_nrm = accessor(b''.join(struct.pack('<3f', *p) for p in normals), '', n, 5126, 'VEC3', 34962)
    a_col = accessor(b''.join(struct.pack('<3f', *p) for p in colours), '', n, 5126, 'VEC3', 34962)
    a_jnt = accessor(b''.join(struct.pack('<4H', *j) for j in joints), '', n, 5123, 'VEC4', 34962)
    a_wgt = accessor(b''.join(struct.pack('<4f', *w) for w in weights), '', n, 5126, 'VEC4', 34962)
    a_idx = accessor(b''.join(struct.pack('<I', i) for i in indices), '', len(indices), 5125, 'SCALAR', 34963)
    # Inverse bind matrices: the T-pose joints are pure translations, so each is a translation back (column-major).
    ibm = b''
    for _, _, head, *_ in BONES:
        ibm += struct.pack('<16f', 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -head[0], -head[1], -head[2], 1)
    a_ibm = accessor(ibm, '', len(BONES), 5126, 'MAT4')

    index = {b[0]: i for i, b in enumerate(BONES)}
    nodes = []
    # Node 0: the character (its mesh, skinned); joints follow from node 1.
    nodes.append({'name': 'Ward', 'mesh': 0, 'skin': 0})
    for name, parent, head, *_ in BONES:
        ph = (0, 0, 0) if parent is None else BONES[index[parent]][2]
        node = {'name': name, 'translation': list(sub(head, ph))}
        if posed and name in POSE:
            node['rotation'] = quat(*POSE[name])
        kids = [i + 1 for i, b in enumerate(BONES) if b[1] == name]
        if kids:
            node['children'] = kids
        nodes.append(node)
    gltf = {
        'asset': {'version': '2.0', 'generator': 'NoorEngine tools/samples/make_test_characters.py'},
        'scene': 0, 'scenes': [{'nodes': [0, 1]}],
        'nodes': nodes,
        'meshes': [{'name': 'Ward', 'primitives': [{'attributes': {'POSITION': a_pos, 'NORMAL': a_nrm, 'COLOR_0': a_col, 'JOINTS_0': a_jnt,
                                                                    'WEIGHTS_0': a_wgt}, 'indices': a_idx, 'material': 0}]}],
        'materials': [{'name': 'Ward', 'pbrMetallicRoughness': {'baseColorFactor': [1, 1, 1, 1], 'metallicFactor': 0, 'roughnessFactor': 0.7}}],
        'skins': [{'name': 'Ward', 'joints': list(range(1, len(BONES) + 1)), 'inverseBindMatrices': a_ibm, 'skeleton': 1}],
        'accessors': accessors, 'bufferViews': views,
    }
    binary = b''.join(blobs)
    while len(binary) % 4:
        binary += b'\0'
    gltf['buffers'] = [{'byteLength': len(binary)}]
    js = json.dumps(gltf, separators=(',', ':')).encode()
    while len(js) % 4:
        js += b' '
    glb = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binary)) + struct.pack('<II', len(js), 0x4E4F534A) + js + \
          struct.pack('<II', len(binary), 0x004E4942) + binary
    path.write_bytes(glb)
    print('wrote', path.relative_to(ROOT), f'({n} vertices, {len(indices) // 3} triangles, {len(BONES)} joints)')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    write_glb(OUT / 'Ward_Bind.glb', posed=False)
    write_glb(OUT / 'Ward_Posed.glb', posed=True)
    SCENE_ASSETS.mkdir(parents=True, exist_ok=True)
    for f in ('Ward_Bind.glb', 'Ward_Posed.glb'):
        shutil.copyfile(OUT / f, SCENE_ASSETS / f)
    shutil.copyfile(ROOT / 'tests' / 'data' / 'animation' / 'RobotExpressive' / 'RobotExpressive.glb', SCENE_ASSETS / 'RobotExpressive.glb')


if __name__ == '__main__':
    main()
