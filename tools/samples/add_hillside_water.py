"""Water on Hillside (Milestone 8D, part 8: the 8D sample).

    python tools/samples/add_hillside_water.py

Writes samples/Hillside/Scenes/Hillside_Water.noorscene: the Hillside lake (add_hillside_lake.py) in light rain, with
- a stream: a channel carved down the gentle slope south-west of the lake, in this scene's own sculpt layer
  (Assets/Terrain/Hillside_Water.noorsculpt, a copy of the lake's), and a river along it into the lake;
- four crates of different densities bobbing in the lake;
- Alden Wayfarer (make_alden.py) walking a loop that wades into the lake, swims across its deep middle (his swim stroke
  comes on from on_water) and walks out the other side.

Everything is added through the editor API (noor_doc_execute, noor_doc_sculpt), as the editor would. Needs a build,
add_hillside_lake.py's scene and make_alden.py's files.
"""
import ctypes
import hashlib
import json
import math
import pathlib
import shutil
import sys
import uuid

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import add_hillside_alden as alden  # noqa: E402  (its API loader and Alden's staff placement)
import add_hillside_storm as storm  # noqa: E402  (execute, call, look_at)

PROJECT = storm.PROJECT
NAMESPACE = uuid.UUID('8d8a7e11-0b4e-4d0d-9c2e-5f5a6c7d8e01')
SCENE = 'Scenes/Hillside_Water.noorscene'
BASE = 'Scenes/Hillside_Lake.noorscene'
SCULPT = 'Assets/Terrain/Hillside_Water.noorsculpt'
LEVEL = 3.30                     # the lake's surface (add_hillside_lake.py)
CENTRE = (61.0, 68.0)            # the lake's middle
UPHILL = (-0.7071, 0.7071)       # the stream comes down from here (the ground rises steadily that way)
STREAM_R = (58.0, 6.0)           # from 58 m out to 6 m (inside the lake)
STREAM_WIDTH = 3.2
BED = 0.45                       # the channel's bed under the stream's surface (m)
# Alden's loop (x, z): he starts on the west shore (the last point) and heads straight across the lake to the east
# shore, then round the south side back, resting a moment where he started.
POINTS = [(76.0, 66.0), (72.0, 50.0), (47.0, 54.0), (45.0, 69.0)]
SPEEDS = [1.4, 1.4, 1.4, 0.0]
REST = 2.0
CRATES = [((58.0, 66.5), 450), ((62.5, 70.0), 600), ((64.0, 65.0), 350), ((59.0, 71.0), 700)]
CRATE_SIZE = 0.5
EYE = (64.5, 4.0, 75.5)
LOOK = (55.0, 2.7, 67.0)


def stream_point(r):
    """The stream's centre r metres out along UPHILL, meandering across it."""
    m = 3.0 * math.sin(r / 9.0)
    across = (UPHILL[1], -UPHILL[0])
    return (CENTRE[0] + UPHILL[0] * r + across[0] * m, CENTRE[1] + UPHILL[1] * r + across[1] * m)


def main():
    for f in ('Alden.glb', 'Staff.glb', 'Alden.nooranim'):
        if not (PROJECT / 'Assets' / 'Characters' / 'Alden' / f).exists():
            sys.exit(f'{f} is missing: run tools/samples/make_alden.py first')
    if not (PROJECT / BASE).exists():
        sys.exit(f'{BASE} is missing: run tools/samples/add_hillside_lake.py first')

    # This scene's own sculpt layer: a copy of the lake's (fresh each run, so reruns give the same channel).
    src = PROJECT / 'Assets' / 'Terrain' / 'Hillside_Lake.noorsculpt'
    dst = PROJECT / SCULPT
    shutil.copyfile(src, dst)
    sculpt_id = str(uuid.uuid5(NAMESPACE, 'Hillside_Water sculpt'))
    meta = {'noor_meta': 1, 'guid': sculpt_id, 'importer': 'terrain_sculpt',
            'source_sha256': hashlib.sha256(dst.read_bytes()).hexdigest(), 'settings': {}}
    (dst.parent / (dst.name + '.noormeta')).write_text(json.dumps(meta, indent=2) + '\n', encoding='utf-8')

    scene = json.loads((PROJECT / BASE).read_text(encoding='utf-8'))
    scene['name'] = 'Hillside_Water'
    scene['id'] = str(uuid.uuid5(NAMESPACE, 'Hillside_Water'))
    terrain = next(e for e in scene['entities'] if 'terrain' in e['components'])
    terrain['components']['terrain']['sculpt'] = sculpt_id
    for e in scene['entities']:
        if 'camera' in e['components']:
            e['transform']['position'] = list(EYE)
            e['transform']['rotation'] = storm.look_at(EYE, LOOK)
        if 'environment' in e['components']:
            e['components']['environment']['rain'] = 0.35  # light rain: rings on the water
            e['components']['environment']['cloud_cover'] = 0.55
    (PROJECT / SCENE).write_text(json.dumps(scene, indent=2) + '\n', encoding='utf-8')

    dll = alden.load_api()
    dll.noor_doc_sculpt.restype = ctypes.c_char_p
    dll.noor_doc_sculpt.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    dll.noor_doc_terrain_height.restype = ctypes.c_int32
    dll.noor_doc_terrain_height.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_float, ctypes.c_float, ctypes.POINTER(ctypes.c_float)]
    doc = dll.noor_doc_open_project(str(PROJECT).encode())
    if not doc:
        sys.exit(f'open project: {dll.noor_last_error().decode()}')
    try:
        listed = json.loads(dll.noor_doc_assets(doc, 1))
        guid = {a['path']: a['guid'] for a in listed}
        model, staff_model, controller = (guid[f'Assets/Characters/Alden/{f}'] for f in ('Alden.glb', 'Staff.glb', 'Alden.nooranim'))
        if dll.noor_doc_open_scene(doc, SCENE.encode()) != 1:
            sys.exit(f'open scene: {dll.noor_last_error().decode()}')
        tid = terrain['id'].encode()

        def height(x, z):
            h = ctypes.c_float()
            if dll.noor_doc_terrain_height(doc, tid, x, z, ctypes.byref(h)) != 1:
                sys.exit(f'height: {dll.noor_last_error().decode()}')
            return h.value

        def stroke(tool, radius, strength, falloff, points, seconds):
            base = {'terrain': terrain['id'], 'tool': tool, 'radius': radius, 'strength': strength, 'falloff': falloff}
            storm.call(dll, 'noor_doc_sculpt', doc, {**base, 'phase': 'begin'})
            dabs = [{'point': [x, height(x, z), z], 'seconds': seconds} for x, z in points]
            storm.call(dll, 'noor_doc_sculpt', doc, {**base, 'phase': 'dab', 'dabs': dabs})
            storm.call(dll, 'noor_doc_sculpt', doc, {**base, 'phase': 'end'})

        # The stream's surface: falling steadily from 45 cm under the ground where it starts to just under the lake's
        # level 9 m from its middle, then on under the lake to its end.
        r0, r1 = STREAM_R
        start = stream_point(r0)
        top = height(*start) - BED

        def surface(r):
            if r >= 9.0:
                return LEVEL - 0.02 + (top - (LEVEL - 0.02)) * (r - 9.0) / (r0 - 9.0)
            return LEVEL - 0.02 - 0.03 * (9.0 - r)

        # Carve: lower the ground along the course until the bed is BED under the surface, then smooth it.
        rs = [r0 - 1.5 * i for i in range(int((r0 - 8.0) / 1.5) + 1)]
        for _ in range(3):
            for r in rs:
                c = stream_point(r)
                for _ in range(60):
                    if height(*c) <= surface(r) - BED:
                        break
                    stroke('lower', 2.0, 1.0, 0.6, [c], 0.12)
            stroke('smooth', 2.6, 0.35, 0.8, [stream_point(r) for r in rs], 0.3)
        # Its banks must stand above its surface (else the ribbon would float over the grass).
        across = (UPHILL[1], -UPHILL[0])
        low_bank = min(min(height(stream_point(r)[0] + across[0] * s * (STREAM_WIDTH / 2 + 1.0),
                                  stream_point(r)[1] + across[1] * s * (STREAM_WIDTH / 2 + 1.0)) - surface(r) for s in (-1, 1)) for r in rs if r > 11)
        deepest = max(height(*stream_point(r)) - surface(r) for r in rs)
        print(f'stream: from {top:.2f} m to the lake; bed at most {deepest:+.2f} m from the surface; banks at least {low_bank:+.2f} m above it')
        if low_bank < 0.05:
            sys.exit('a bank of the stream is below its surface: the water would float over the grass')

        river = str(uuid.uuid5(NAMESPACE, 'Stream'))
        points = []
        r = r0
        while r >= r1 - 1e-6:
            x, z = stream_point(r)
            points.append([round(x, 3), round(surface(r), 3), round(z, 3)])
            r -= 4.0
        storm.execute(dll, doc, {'op': 'create', 'id': river, 'name': 'Stream', 'components': {
            'water': {'shape': 'river', 'preset': 'River', 'points': points, 'width': STREAM_WIDTH, 'speed': 1.2}}})

        # Crates bobbing in the lake.
        for i, ((x, z), density) in enumerate(CRATES):
            mass = round(density * CRATE_SIZE ** 3, 3)
            storm.execute(dll, doc, {'op': 'create', 'id': str(uuid.uuid5(NAMESPACE, f'Crate {i}')), 'name': f'Crate {i + 1}',
                                     'transform': {'position': [x, LEVEL + 0.4, z], 'rotation': [0, math.sin(i * 0.4), 0, math.cos(i * 0.4)]},
                                     'components': {'primitive': {'shape': 'box', 'size': [CRATE_SIZE] * 3, 'color': '#A57A45'},
                                                    'collider': {'shape': 'box', 'size': [CRATE_SIZE] * 3},
                                                    'rigidbody': {'type': 'dynamic', 'mass': mass}}})

        # Alden: a character controller with the wandering script; Alden (the model, his controller with root motion)
        # its child; his staff on his right hand (as add_hillside_alden.py).
        def entities():
            return json.loads(dll.noor_doc_scene(doc))['entities']

        sx, sz = POINTS[-1]
        sy = height(sx, sz)
        walker = str(uuid.uuid5(NAMESPACE, 'Alden walker'))
        first_leg = math.degrees(math.atan2(POINTS[0][0] - POINTS[-1][0], POINTS[0][1] - POINTS[-1][1]))
        storm.execute(dll, doc, {'op': 'create', 'id': walker, 'name': 'Alden Wayfarer', 'transform': {'position': [sx, sy + 0.05, sz]},
                                 'components': {'character': {'radius': 0.3, 'height': 1.85, 'step_height': 0.4, 'max_slope': 50},
                                                'script': {'file': 'Scripts/alden_wander.luau',
                                                           'params': {'points': [list(p) for p in POINTS], 'speeds': SPEEDS, 'rest': REST,
                                                                      'turn': 150, 'accel': 2.5, 'start_facing': round(first_leg, 3)}}}})
        before = {e['id'] for e in entities()}
        storm.execute(dll, doc, {'op': 'add_asset', 'asset': model, 'position': [sx, sy, sz], 'name': 'Alden'})
        model_e = next(e for e in entities() if e['id'] not in before and e['name'] == 'Alden' and e.get('parent') is None)
        storm.execute(dll, doc, {'op': 'reparent', 'entity': model_e['id'], 'parent': walker})
        storm.execute(dll, doc, {'op': 'set_transform', 'entity': model_e['id'], 'transform': {'position': [0, 0, 0]}})
        storm.execute(dll, doc, {'op': 'set_component', 'entity': model_e['id'], 'type': 'animator',
                                 'value': {'controller': controller, 'root_motion': True, 'parameters': {'speed': 0, 'swimming': False}}})
        hand = model_e['components']['model']['nodes'][alden.HAND_NODE]
        before = {e['id'] for e in entities()}
        storm.execute(dll, doc, {'op': 'add_asset', 'asset': staff_model, 'position': [sx, sy, sz], 'name': 'Staff'})
        staff = next(e for e in entities() if e['id'] not in before and e['name'] == 'Staff' and e.get('parent') is None)
        storm.execute(dll, doc, {'op': 'reparent', 'entity': staff['id'], 'parent': hand})
        h = math.radians(alden.STAFF_TURN_DEG) / 2
        storm.execute(dll, doc, {'op': 'set_transform', 'entity': staff['id'],
                                 'transform': {'position': alden.STAFF_AT, 'rotation': [0, 0, math.sin(h), math.cos(h)], 'scale': [1, 1, 1]}})
        if dll.noor_doc_save_scene(doc) != 1:
            sys.exit(f'save: {dll.noor_last_error().decode()}')
    finally:
        dll.noor_doc_close(doc)
    print('wrote', SCENE, 'and', SCULPT)


if __name__ == '__main__':
    main()
