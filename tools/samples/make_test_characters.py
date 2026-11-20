"""Test characters for skeletal animation (Milestone 8C), made here so they're ours (MIT, like the repository).

    python tools/samples/make_test_characters.py

Writes into tests/data/animation/Ward/ (and copies into tests/data/render_scenes/Assets/Characters/ for goldens):

  Ward_Bind.glb   "Ward", a smoothly skinned humanoid, 1.8 m: 17 joints with Rigify-style deform names (DEF-spine,
                  DEF-upper_arm.L ...), bound in a T-pose. The file's own pose is the bind pose, so it draws as bound.
  Ward_Posed.glb  the same mesh and skin, but its joints' transforms are posed (arms down, the left elbow and right knee
                  bent, the head turned): before any animation exists, drawing it proves the whole skinning path.
  Ward_Anim.glb   Ward bound and at rest in the T-pose, with clips (part 2), each with answers worked out by hand:
                    Wave   LINEAR rotation: the right upper arm 0 -> 90 degrees about Z over 1 s (45 at 0.5 s);
                    Nod    STEP rotation: the head 0 degrees until 0.5 s, then 30 about Y;
                    Glide  CUBICSPLINE translation: the hips 1 m forward (+Z) over 1 s, leaving at 2 m/s and arriving
                           still (0.4375 m at 0.25 s, 0.75 m at 0.5 s);
                    Walk   a 1 s walk cycle, first key = last (loops): legs and arms swing (arms down), hips bob.
                  and for blending (part 3):
                    Idle   a 2 s breath: arms down, the chest rising 3 degrees;
                    Run    a 0.6 s run cycle: bigger swings, knees high, leaning forward;
                    Raise  a 1 s wave hello: the right arm up, its forearm sweeping (for a masked layer);
                    Lean   spine 20 and chest 10 degrees forward over 1 s (for an additive layer: the change from its
                           first key is what gets added);
                  and for root motion (part 4):
                    WalkForward  Walk's legs and arms, with the hips moving 1.4 m forward (+Z) each 1 s cycle: played
                           with root motion it walks the character 1.4 m a second; without, it pops back each loop.

And into tests/data/animation/Retarget/ (part 5), two more bodies with no clips of their own, to retarget Ward's onto:

  Mast.glb        tall and thin (2.0 m), Mixamo's names (mixamorig:Hips, mixamorig:LeftArm ...), a T-pose, and Mixamo's
                  units: the bones in centimetres under an "Armature" node scaled 0.01; shoulders, three spine bones and
                  toes. Green.
  Bollard.glb     short and stout (1.45 m), Rigify's deform names (DEF-spine for the hips, DEF-spine.001 ... .006 for
                  spine, chest, neck and head, DEF-upper_arm.L.001 twist bones), built in an A-pose (arms 45 degrees
                  down), with Blender's bone frames (each joint's +Y along its bone, rolled; the forearms rolled 30
                  degrees more), so every joint has a rest rotation. Rust red.

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
RETARGET = ROOT / 'tests' / 'data' / 'animation' / 'Retarget'
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


def _mast():
    """Mast: Mixamo names, T-pose, 2.0 m."""
    green = (0.22, 0.48, 0.32)
    p = 'mixamorig:'
    bones = [
        (p + 'Hips', None, (0, 1.10, 0), 0.15, 0.14, green),
        (p + 'Spine', p + 'Hips', (0, 1.25, 0), 0.14, 0.15, green),
        (p + 'Spine1', p + 'Spine', (0, 1.42, 0), 0.15, 0.16, green),
        (p + 'Spine2', p + 'Spine1', (0, 1.58, 0), 0.16, 0.13, green),
        (p + 'Neck', p + 'Spine2', (0, 1.72, 0), 0.05, 0.05, SKIN),
        (p + 'Head', p + 'Neck', (0, 1.80, 0), 0.10, 0.08, SKIN),
    ]
    for side, s in (('Left', 1), ('Right', -1)):
        bones += [
            (p + side + 'Shoulder', p + 'Spine2', (0.05 * s, 1.66, 0), 0.05, 0.05, green),
            (p + side + 'Arm', p + side + 'Shoulder', (0.20 * s, 1.66, 0), 0.05, 0.045, LIMB),
            (p + side + 'ForeArm', p + side + 'Arm', (0.52 * s, 1.66, 0), 0.045, 0.035, LIMB),
            (p + side + 'Hand', p + side + 'ForeArm', (0.82 * s, 1.66, 0), 0.035, 0.03, SKIN),
        ]
    for side, s in (('Left', 1), ('Right', -1)):
        bones += [
            (p + side + 'UpLeg', p + 'Hips', (0.11 * s, 1.06, 0), 0.07, 0.055, LIMB),
            (p + side + 'Leg', p + side + 'UpLeg', (0.11 * s, 0.58, 0), 0.055, 0.04, LIMB),
            (p + side + 'Foot', p + side + 'Leg', (0.11 * s, 0.09, 0), 0.04, 0.035, green),
            (p + side + 'ToeBase', p + side + 'Foot', (0.11 * s, 0.03, 0.12), 0.03, 0.025, green),
        ]
    tails = {p + 'Head': (0, 2.02, 0), p + 'LeftHand': (0.95, 1.66, 0), p + 'RightHand': (-0.95, 1.66, 0),
             p + 'LeftToeBase': (0.11, 0.03, 0.22), p + 'RightToeBase': (-0.11, 0.03, 0.22)}
    return {'name': 'Mast', 'bones': bones, 'tails': tails, 'frames': 'identity', 'armature_scale': 0.01, 'rolls': {}}


def _bollard():
    """Bollard: Rigify deform names, A-pose, Blender bone frames, 1.45 m."""
    rust = (0.62, 0.30, 0.18)
    d = 'DEF-'
    bones = [
        (d + 'spine', None, (0, 0.78, 0), 0.20, 0.21, rust),
        (d + 'spine.001', d + 'spine', (0, 0.90, 0), 0.21, 0.22, rust),
        (d + 'spine.002', d + 'spine.001', (0, 1.02, 0), 0.22, 0.21, rust),
        (d + 'spine.003', d + 'spine.002', (0, 1.12, 0), 0.21, 0.15, rust),
        (d + 'spine.004', d + 'spine.003', (0, 1.26, 0), 0.07, 0.07, SKIN),
        (d + 'spine.005', d + 'spine.004', (0, 1.30, 0), 0.07, 0.07, SKIN),
        (d + 'spine.006', d + 'spine.005', (0, 1.33, 0), 0.12, 0.10, SKIN),
    ]
    c45 = math.sqrt(0.5)
    for side, s in (('L', 1), ('R', -1)):
        def arm(k):  # a point k metres along the arm from the shoulder joint, 45 degrees down
            return (s * (0.21 + c45 * k), 1.24 - c45 * k, 0)
        bones += [
            (d + 'shoulder.' + side, d + 'spine.003', (0.03 * s, 1.22, 0.02), 0.07, 0.07, rust),
            (d + 'upper_arm.' + side, d + 'shoulder.' + side, arm(0), 0.065, 0.06, LIMB),
            (d + 'upper_arm.' + side + '.001', d + 'upper_arm.' + side, arm(0.12), 0.06, 0.055, LIMB),
            (d + 'forearm.' + side, d + 'upper_arm.' + side + '.001', arm(0.24), 0.055, 0.045, LIMB),
            (d + 'hand.' + side, d + 'forearm.' + side, arm(0.46), 0.045, 0.04, SKIN),
        ]
    for side, s in (('L', 1), ('R', -1)):
        bones += [
            (d + 'thigh.' + side, d + 'spine', (0.12 * s, 0.76, 0), 0.10, 0.08, LIMB),
            (d + 'shin.' + side, d + 'thigh.' + side, (0.12 * s, 0.43, 0.01), 0.08, 0.06, LIMB),
            (d + 'foot.' + side, d + 'shin.' + side, (0.12 * s, 0.08, 0), 0.06, 0.05, rust),
            (d + 'toe.' + side, d + 'foot.' + side, (0.12 * s, 0.03, 0.11), 0.045, 0.04, rust),
        ]
    tails = {d + 'spine.006': (0, 1.53, 0), d + 'hand.L': (0.21 + c45 * 0.56, 1.24 - c45 * 0.56, 0),
             d + 'hand.R': (-0.21 - c45 * 0.56, 1.24 - c45 * 0.56, 0), d + 'toe.L': (0.12, 0.03, 0.19), d + 'toe.R': (-0.12, 0.03, 0.19)}
    rolls = {d + 'forearm.L': 30, d + 'forearm.R': -30}
    return {'name': 'Bollard', 'bones': bones, 'tails': tails, 'frames': 'blender', 'armature_scale': None, 'rolls': rolls}


WARD = {'name': 'Ward', 'bones': BONES, 'tails': TAILS, 'frames': 'identity', 'armature_scale': None, 'rolls': {}}
MAST = _mast()
BOLLARD = _bollard()

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


def build_mesh(bones=BONES, tails=TAILS):
    index = {b[0]: i for i, b in enumerate(bones)}
    children = {}
    for b in bones:
        if b[1]:
            children.setdefault(b[1], []).append(b[0])
    positions, normals, colours, joints, weights, indices = [], [], [], [], [], []
    for bi, (name, parent, head, r0, r1, colour) in enumerate(bones):
        tail = tails.get(name) or [h for n, _, h, *_ in bones if n == children[name][0]][0]
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


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz]


def clips():
    """Ward_Anim's clips: (name, [(bone, path, interpolation, times, values per key)])."""
    walk_t = [0, 0.25, 0.5, 0.75, 1.0]
    swing = [30, 0, -30, 0, 30]
    arm_l = [qmul(quat((1, 0, 0), -a * 0.6), quat((0, 0, 1), -70)) for a in swing]  # down, then swinging against the leg
    arm_r = [qmul(quat((1, 0, 0), a * 0.6), quat((0, 0, 1), 70)) for a in swing]
    run_t = [0, 0.15, 0.3, 0.45, 0.6]
    run_swing = [50, 0, -50, 0, 50]
    return [
        ('Wave', [('DEF-upper_arm.R', 'rotation', 'LINEAR', [0, 1], [quat((0, 0, 1), 0), quat((0, 0, 1), 90)])]),
        ('Nod', [('DEF-head', 'rotation', 'STEP', [0, 0.5, 1.0], [quat((0, 1, 0), 0), quat((0, 1, 0), 30), quat((0, 1, 0), 30)])]),
        # Cubic spline values per key: in-tangent, value, out-tangent. Leaves at 2 m/s along +Z, arrives still.
        ('Glide', [('DEF-hips', 'translation', 'CUBICSPLINE', [0, 1],
                    [[0, 0, 0], [0, 0.95, 0], [0, 0, 2], [0, 0, 0], [0, 0.95, 1], [0, 0, 0]])]),
        ('Walk', [
            ('DEF-thigh.L', 'rotation', 'LINEAR', walk_t, [quat((1, 0, 0), -a) for a in swing]),
            ('DEF-thigh.R', 'rotation', 'LINEAR', walk_t, [quat((1, 0, 0), a) for a in swing]),
            ('DEF-shin.L', 'rotation', 'LINEAR', walk_t, [quat((1, 0, 0), b) for b in (0, 10, 40, 10, 0)]),
            ('DEF-shin.R', 'rotation', 'LINEAR', walk_t, [quat((1, 0, 0), b) for b in (40, 10, 0, 10, 40)]),
            ('DEF-upper_arm.L', 'rotation', 'LINEAR', walk_t, arm_l),
            ('DEF-upper_arm.R', 'rotation', 'LINEAR', walk_t, arm_r),
            ('DEF-hips', 'translation', 'LINEAR', walk_t, [[0, y, 0] for y in (0.95, 0.93, 0.95, 0.93, 0.95)]),
        ]),
        ('WalkForward', [
            ('DEF-thigh.L', 'rotation', 'LINEAR', walk_t, [quat((1, 0, 0), -a) for a in swing]),
            ('DEF-thigh.R', 'rotation', 'LINEAR', walk_t, [quat((1, 0, 0), a) for a in swing]),
            ('DEF-shin.L', 'rotation', 'LINEAR', walk_t, [quat((1, 0, 0), b) for b in (0, 10, 40, 10, 0)]),
            ('DEF-shin.R', 'rotation', 'LINEAR', walk_t, [quat((1, 0, 0), b) for b in (40, 10, 0, 10, 40)]),
            ('DEF-upper_arm.L', 'rotation', 'LINEAR', walk_t, arm_l),
            ('DEF-upper_arm.R', 'rotation', 'LINEAR', walk_t, arm_r),
            ('DEF-hips', 'translation', 'LINEAR', walk_t, [[0, y, z] for y, z in zip((0.95, 0.93, 0.95, 0.93, 0.95), (0, 0.35, 0.7, 1.05, 1.4))]),
        ]),
        ('Idle', [
            ('DEF-chest', 'rotation', 'LINEAR', [0, 1, 2], [quat((1, 0, 0), b) for b in (0, -3, 0)]),
            ('DEF-upper_arm.L', 'rotation', 'LINEAR', [0, 1, 2], [qmul(quat((1, 0, 0), b), quat((0, 0, 1), -72)) for b in (0, 3, 0)]),
            ('DEF-upper_arm.R', 'rotation', 'LINEAR', [0, 1, 2], [qmul(quat((1, 0, 0), b), quat((0, 0, 1), 72)) for b in (0, 3, 0)]),
            ('DEF-hips', 'translation', 'LINEAR', [0, 1, 2], [[0, y, 0] for y in (0.95, 0.947, 0.95)]),
        ]),
        ('Run', [
            ('DEF-thigh.L', 'rotation', 'LINEAR', run_t, [quat((1, 0, 0), -a) for a in run_swing]),
            ('DEF-thigh.R', 'rotation', 'LINEAR', run_t, [quat((1, 0, 0), a) for a in run_swing]),
            ('DEF-shin.L', 'rotation', 'LINEAR', run_t, [quat((1, 0, 0), b) for b in (10, 40, 95, 40, 10)]),
            ('DEF-shin.R', 'rotation', 'LINEAR', run_t, [quat((1, 0, 0), b) for b in (95, 40, 10, 40, 95)]),
            ('DEF-upper_arm.L', 'rotation', 'LINEAR', run_t, [qmul(quat((1, 0, 0), -a * 0.7), quat((0, 0, 1), -75)) for a in run_swing]),
            ('DEF-upper_arm.R', 'rotation', 'LINEAR', run_t, [qmul(quat((1, 0, 0), a * 0.7), quat((0, 0, 1), 75)) for a in run_swing]),
            ('DEF-chest', 'rotation', 'LINEAR', run_t, [quat((1, 0, 0), 10)] * 5),
            ('DEF-hips', 'translation', 'LINEAR', run_t, [[0, y, 0] for y in (0.92, 0.89, 0.92, 0.89, 0.92)]),
        ]),
        ('Raise', [
            ('DEF-upper_arm.R', 'rotation', 'LINEAR', walk_t, [quat((0, 0, 1), -60)] * 5),
            ('DEF-forearm.R', 'rotation', 'LINEAR', walk_t, [quat((0, 0, 1), b) for b in (-20, -50, -20, 10, -20)]),
        ]),
        ('Lean', [
            ('DEF-spine', 'rotation', 'LINEAR', [0, 1], [quat((1, 0, 0), 0), quat((1, 0, 0), 20)]),
            ('DEF-chest', 'rotation', 'LINEAR', [0, 1], [quat((1, 0, 0), 0), quat((1, 0, 0), 10)]),
        ]),
    ]


def frame(d, roll_degrees):
    """Blender's bone frame: +Y along the bone d, +X and +Z across it (rolled by roll_degrees); columns x, y, z."""
    ref = (0, 0, 1) if abs(d[2]) < 0.9 else (0, 1, 0)
    x = norm(cross(d, ref))
    z = cross(x, d)
    a = math.radians(roll_degrees)
    x, z = add(mul(x, math.cos(a)), mul(z, math.sin(a))), add(mul(z, math.cos(a)), mul(x, -math.sin(a)))
    return x, d, z


def quat_from_columns(x, y, z):
    m00, m01, m02 = x[0], y[0], z[0]
    m10, m11, m12 = x[1], y[1], z[1]
    m20, m21, m22 = x[2], y[2], z[2]
    tr = m00 + m11 + m22
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        q = [(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s]
    elif m00 > m11 and m00 > m22:
        s = math.sqrt(1.0 + m00 - m11 - m22) * 2
        q = [0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s]
    elif m11 > m22:
        s = math.sqrt(1.0 + m11 - m00 - m22) * 2
        q = [(m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s]
    else:
        s = math.sqrt(1.0 + m22 - m00 - m11) * 2
        q = [(m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s]
    return q


def qconj(q):
    return [-q[0], -q[1], -q[2], q[3]]


def qrot(q, v):
    p = qmul(qmul(q, [v[0], v[1], v[2], 0.0]), qconj(q))
    return tuple(p[:3])


def write_glb(path, posed, animated=False, rig=WARD):
    bones, tails = rig['bones'], rig['tails']
    positions, normals, colours, joints, weights, indices = build_mesh(bones, tails)
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
    index = {b[0]: i for i, b in enumerate(bones)}
    children = {}
    for b in bones:
        if b[1]:
            children.setdefault(b[1], []).append(b[0])
    # Each joint's rest frame in model space: none (Ward, Mast: pure translations) or Blender's (Bollard).
    rot = {}
    for name, parent, head, *_ in bones:
        if rig['frames'] == 'blender':
            tail = tails.get(name) or bones[index[children[name][0]]][2]
            rot[name] = quat_from_columns(*frame(norm(sub(tail, head)), rig['rolls'].get(name, 0)))
        else:
            rot[name] = [0, 0, 0, 1]
    k = rig['armature_scale'] or 1.0  # Mast: bones in centimetres under an Armature scaled 0.01
    first = 2 if rig['armature_scale'] else 1  # the first joint's node
    # Inverse bind matrices: back from model space into each joint's (column-major). For pure translations (Ward), a
    # translation back.
    ibm = b''
    for name, _, head, *_ in bones:
        q = rot[name]
        ix, iy, iz = qrot(qconj(q), (1, 0, 0)), qrot(qconj(q), (0, 1, 0)), qrot(qconj(q), (0, 0, 1))  # R^T's columns
        t = qrot(qconj(q), head)
        cols = [mul(ix, 1 / k), mul(iy, 1 / k), mul(iz, 1 / k), mul(t, -1 / k)]
        if rig['frames'] != 'blender' and k == 1.0:
            cols = [(1, 0, 0), (0, 1, 0), (0, 0, 1), (-head[0], -head[1], -head[2])]
        ibm += struct.pack('<16f', *cols[0], 0, *cols[1], 0, *cols[2], 0, *cols[3], 1)
    a_ibm = accessor(ibm, '', len(bones), 5126, 'MAT4')

    nodes = []
    # Node 0: the character (its mesh, skinned); joints follow (after the Armature node, if there is one).
    nodes.append({'name': rig['name'], 'mesh': 0, 'skin': 0})
    if rig['armature_scale']:
        nodes.append({'name': 'Armature', 'scale': [k, k, k], 'children': [first]})
    for name, parent, head, *_ in bones:
        ph = (0, 0, 0) if parent is None else bones[index[parent]][2]
        pq = [0, 0, 0, 1] if parent is None else rot[parent]
        offset = qrot(qconj(pq), sub(head, ph)) if rig['frames'] == 'blender' else sub(head, ph)
        node = {'name': name, 'translation': list(mul(offset, 1 / k)) if k != 1.0 else list(offset)}
        local = qmul(qconj(pq), rot[name])
        if rig['frames'] == 'blender':
            node['rotation'] = local
        if posed and name in POSE:
            node['rotation'] = quat(*POSE[name])
        kids = [i + first for i, b in enumerate(bones) if b[1] == name]
        if kids:
            node['children'] = kids
        nodes.append(node)
    animations = []
    if animated:
        index_of = {b[0]: i + first for i, b in enumerate(bones)}  # glTF node index (node 0 is the mesh)
        for name, channels in clips():
            samplers, chans = [], []
            for bone, target, interp, times, values in channels:
                a_in = accessor(b''.join(struct.pack('<f', t) for t in times), '', len(times), 5126, 'SCALAR', None, ([min(times)], [max(times)]))
                flat = [v for value in values for v in value]
                kind = 'VEC4' if target == 'rotation' else 'VEC3'
                a_out = accessor(b''.join(struct.pack('<f', v) for v in flat), '', len(values), 5126, kind)
                samplers.append({'input': a_in, 'output': a_out, 'interpolation': interp})
                chans.append({'sampler': len(samplers) - 1, 'target': {'node': index_of[bone], 'path': target}})
            animations.append({'name': name, 'samplers': samplers, 'channels': chans})

    gltf = {
        'asset': {'version': '2.0', 'generator': 'NoorEngine tools/samples/make_test_characters.py'},
        'scene': 0, 'scenes': [{'nodes': [0, 1]}],
        'nodes': nodes,
        'meshes': [{'name': rig['name'], 'primitives': [{'attributes': {'POSITION': a_pos, 'NORMAL': a_nrm, 'COLOR_0': a_col, 'JOINTS_0': a_jnt,
                                                                    'WEIGHTS_0': a_wgt}, 'indices': a_idx, 'material': 0}]}],
        'materials': [{'name': rig['name'], 'pbrMetallicRoughness': {'baseColorFactor': [1, 1, 1, 1], 'metallicFactor': 0, 'roughnessFactor': 0.7}}],
        'skins': [{'name': rig['name'], 'joints': list(range(first, len(bones) + first)), 'inverseBindMatrices': a_ibm, 'skeleton': first}],
        'accessors': accessors, 'bufferViews': views,
        **({'animations': animations} if animations else {}),
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
    print('wrote', path.relative_to(ROOT), f'({n} vertices, {len(indices) // 3} triangles, {len(bones)} joints)')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    write_glb(OUT / 'Ward_Bind.glb', posed=False)
    write_glb(OUT / 'Ward_Posed.glb', posed=True)
    write_glb(OUT / 'Ward_Anim.glb', posed=False, animated=True)
    RETARGET.mkdir(parents=True, exist_ok=True)
    write_glb(RETARGET / 'Mast.glb', posed=False, rig=MAST)
    write_glb(RETARGET / 'Bollard.glb', posed=False, rig=BOLLARD)
    SCENE_ASSETS.mkdir(parents=True, exist_ok=True)
    for f in ('Ward_Bind.glb', 'Ward_Posed.glb', 'Ward_Anim.glb'):
        shutil.copyfile(OUT / f, SCENE_ASSETS / f)
    for f in ('Mast.glb', 'Bollard.glb'):
        shutil.copyfile(RETARGET / f, SCENE_ASSETS / f)
    shutil.copyfile(ROOT / 'tests' / 'data' / 'animation' / 'RobotExpressive' / 'RobotExpressive.glb', SCENE_ASSETS / 'RobotExpressive.glb')


if __name__ == '__main__':
    main()
