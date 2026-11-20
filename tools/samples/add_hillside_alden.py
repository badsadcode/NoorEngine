"""Alden Wayfarer walks about Hillside (Milestone 8C, part 8: the 8C sample).

    python tools/samples/add_hillside_alden.py

Writes samples/Hillside/Scenes/Hillside_Alden.noorscene: the valley in fair weather, and Alden (make_alden.py) on a
character controller wandering a loop by the house: he walks, runs a stretch, rests and walks on. His animator runs his
controller with **root motion**: Idle, WalkForward and RunForward blended by "speed", so his clips move him (the
controller stops him at walls, the slope lifts him); his script (Scripts/alden_wander.luau) only steers and sets the
pace. His staff is a separate model on a socket: a child of his right hand's bone.

Everything is added through the editor API (noor_doc_execute), as the editor would; the ground heights come from a
throwaway probe scene (add_hillside_storm.ground_heights). Needs a build and make_alden.py's files.
"""
import json
import math
import pathlib
import sys
import uuid

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import add_hillside_storm as storm  # noqa: E402  (its API loader, ground probe and look_at)

ROOT = storm.ROOT
PROJECT = storm.PROJECT
NAMESPACE = uuid.UUID('8c0c1d2e-3f40-4152-a637-48596a7b8c9e')
SCENE = 'Scenes/Hillside_Alden.noorscene'
BASE = 'Scenes/Presets/Hillside_FairWeather.noorscene'
# The loop (x, z), by the house, and each leg's pace (m/s; 0: rest at its point, then on at the next leg's).
POINTS = [(21.0, -18.0), (31.0, -18.5), (32.0, -9.0), (22.0, -8.5)]
SPEEDS = [1.4, 4.0, 1.4, 0.0]
REST = 2.0
HAND_NODE = 11  # DEF-hand.R in Alden.glb (node 0 is his mesh)
# The staff in his hand, in the hand bone's frame (the bind pose: the arm along -X): the grip in his fist, the pole
# turned upright for an arm hanging at his side (Idle and the walk hold it about 72 degrees down).
STAFF_AT = [-0.06, 0.0, 0.035]
STAFF_TURN_DEG = -72.0


def load_api():
    dll = storm.load_api()
    import ctypes
    S, P, I = ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int32
    for name, res, args in [('noor_doc_assets', S, [P, I]), ('noor_doc_scene', S, [P])]:
        fn = getattr(dll, name)
        fn.restype = res
        fn.argtypes = args
    return dll


def main():
    assets = PROJECT / 'Assets' / 'Characters' / 'Alden'
    for f in ('Alden.glb', 'Staff.glb', 'Alden.nooranim'):
        if not (assets / f).exists():
            sys.exit(f'{f} is missing: run tools/samples/make_alden.py first')
    heights = storm.ground_heights(POINTS)
    sx, sz = POINTS[-1]  # he starts at the last point (where he rests), heading for the first
    sy = heights[POINTS[-1]]
    print(f'Alden starts at y = {sy:.3f}')

    scene = json.loads((PROJECT / BASE).read_text(encoding='utf-8'))
    scene['name'] = 'Hillside_Alden'
    scene['id'] = str(uuid.uuid5(NAMESPACE, 'Hillside_Alden'))
    cx = sum(p[0] for p in POINTS) / len(POINTS)
    cz = sum(p[1] for p in POINTS) / len(POINTS)
    cy = sum(heights[p] for p in POINTS) / len(POINTS)
    eye = [cx - 4.5, cy + 1.9, cz - 11.5]  # down the slope from the running leg, about 6 m from it
    for e in scene['entities']:
        if 'camera' in e['components']:
            e['transform']['position'] = [round(v, 4) for v in eye]
            e['transform']['rotation'] = storm.look_at(eye, [cx + 0.5, cy + 0.9, cz - 2.5])
    (PROJECT / SCENE).write_text(json.dumps(scene, indent=2) + '\n', encoding='utf-8')

    dll = load_api()
    doc = dll.noor_doc_open_project(str(PROJECT).encode())
    if not doc:
        sys.exit(f'open project: {dll.noor_last_error().decode()}')
    try:
        listed = json.loads(dll.noor_doc_assets(doc, 1))
        guid = {a['path']: a['guid'] for a in listed}
        model, staff_model, controller = (guid[f'Assets/Characters/Alden/{f}'] for f in ('Alden.glb', 'Staff.glb', 'Alden.nooranim'))
        if dll.noor_doc_open_scene(doc, SCENE.encode()) != 1:
            sys.exit(f'open scene: {dll.noor_last_error().decode()}')

        def entities():
            return json.loads(dll.noor_doc_scene(doc))['entities']

        # The walker: a character controller with the wandering script; Alden (the model) its child.
        walker = str(uuid.uuid5(NAMESPACE, 'Alden walker'))
        first_leg = math.degrees(math.atan2(POINTS[0][0] - POINTS[-1][0], POINTS[0][1] - POINTS[-1][1]))
        storm.execute(dll, doc, {'op': 'create', 'id': walker, 'name': 'Alden Wayfarer', 'transform': {'position': [sx, sy + 0.05, sz]},
                                 'components': {'character': {'radius': 0.3, 'height': 1.85, 'step_height': 0.4, 'max_slope': 50},
                                                'script': {'file': 'Scripts/alden_wander.luau',
                                                           'params': {'points': [list(p) for p in POINTS], 'speeds': SPEEDS, 'rest': REST,
                                                                      'turn': 150, 'accel': 2.5, 'start_facing': round(first_leg, 3)}}}})
        before = {e['id'] for e in entities()}
        storm.execute(dll, doc, {'op': 'add_asset', 'asset': model, 'position': [sx, sy, sz], 'name': 'Alden'})
        alden = next(e for e in entities() if e['id'] not in before and e['name'] == 'Alden' and e.get('parent') is None)
        storm.execute(dll, doc, {'op': 'reparent', 'entity': alden['id'], 'parent': walker})
        storm.execute(dll, doc, {'op': 'set_transform', 'entity': alden['id'], 'transform': {'position': [0, 0, 0]}})
        storm.execute(dll, doc, {'op': 'set_component', 'entity': alden['id'], 'type': 'animator',
                                 'value': {'controller': controller, 'root_motion': True, 'parameters': {'speed': 0}}})
        # His staff: a separate model on a socket (a child of his right hand's bone entity).
        hand = alden['components']['model']['nodes'][HAND_NODE]
        before = {e['id'] for e in entities()}
        storm.execute(dll, doc, {'op': 'add_asset', 'asset': staff_model, 'position': [sx, sy, sz], 'name': 'Staff'})
        staff = next(e for e in entities() if e['id'] not in before and e['name'] == 'Staff' and e.get('parent') is None)
        storm.execute(dll, doc, {'op': 'reparent', 'entity': staff['id'], 'parent': hand})
        h = math.radians(STAFF_TURN_DEG) / 2
        storm.execute(dll, doc, {'op': 'set_transform', 'entity': staff['id'],
                                 'transform': {'position': STAFF_AT, 'rotation': [0, 0, math.sin(h), math.cos(h)], 'scale': [1, 1, 1]}})
        if dll.noor_doc_save_scene(doc) != 1:
            sys.exit(f'save: {dll.noor_last_error().decode()}')
    finally:
        dll.noor_doc_close(doc)
    print('wrote', SCENE)


if __name__ == '__main__':
    main()
