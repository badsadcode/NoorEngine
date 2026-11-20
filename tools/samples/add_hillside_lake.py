"""A calm lake on Hillside (Milestone 8D, part 1: the water surface).

    python tools/samples/add_hillside_lake.py

Writes samples/Hillside/Scenes/Hillside_Lake.noorscene: the valley in fair weather with a lake in the shallow dip north
of the house (around x 60, z 68). The valley floor there is nearly flat, so the lake digs its own hollow: the scene's
terrain gets its own sculpt layer (Assets/Terrain/Hillside_Lake.noorsculpt, a copy of Hillside's), lowered with the
sculpt brush through the editor API as the editor would, so the other Hillside scenes keep their ground. The water is
a `water` component (the "Lake" preset) on a level rectangle whose edges all sit on ground above the water.

Needs a build (noor_editor_api.dll).
"""
import ctypes
import hashlib
import json
import pathlib
import shutil
import sys
import uuid

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import add_hillside_storm as storm  # noqa: E402  (its API loader, execute and look_at)

ROOT = storm.ROOT
PROJECT = storm.PROJECT
NAMESPACE = uuid.UUID('8d0a7e11-1a4e-4c0d-9b2e-3f5a6c7d8e9f')
SCENE = 'Scenes/Hillside_Lake.noorscene'
BASE = 'Scenes/Presets/Hillside_FairWeather.noorscene'
SCULPT = 'Assets/Terrain/Hillside_Lake.noorsculpt'
LEVEL = 3.30            # the water's surface (world y); the ground around the rectangle is above it
RECT = (44.0, 76.0, 52.0, 84.0)  # the water rectangle: x from, x to, z from, z to
FLOOR = 1.55            # dig until the deepest point is about this low (so about 1.75 m of water)
# The hollow: dabs of the "lower" brush along a lopsided loop, so the shore isn't a circle.
DABS = [(55.0, 63.0), (58.5, 70.0), (63.5, 73.0), (67.0, 67.5), (63.0, 62.5), (59.0, 66.5)]
EYE = (56.0, 6.5, 81.0)
LOOK = (62.0, 2.5, 65.0)


def main():
    dll = storm.load_api()
    dll.noor_doc_sculpt.restype = ctypes.c_char_p
    dll.noor_doc_sculpt.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    dll.noor_doc_terrain_height.restype = ctypes.c_int32
    dll.noor_doc_terrain_height.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_float, ctypes.c_float, ctypes.POINTER(ctypes.c_float)]

    # The lake's own sculpt layer: a copy of Hillside's (fresh each run, so reruns give the same hollow).
    src = PROJECT / 'Assets' / 'Terrain' / 'Hillside.noorsculpt'
    dst = PROJECT / SCULPT
    shutil.copyfile(src, dst)
    sculpt_id = str(uuid.uuid5(NAMESPACE, 'Hillside_Lake sculpt'))
    meta = {'noor_meta': 1, 'guid': sculpt_id, 'importer': 'terrain_sculpt',
            'source_sha256': hashlib.sha256(dst.read_bytes()).hexdigest(), 'settings': {}}
    (dst.parent / (dst.name + '.noormeta')).write_text(json.dumps(meta, indent=2) + '\n', encoding='utf-8')

    scene = json.loads((PROJECT / BASE).read_text(encoding='utf-8'))
    scene['name'] = 'Hillside_Lake'
    scene['id'] = str(uuid.uuid5(NAMESPACE, 'Hillside_Lake'))
    terrain = next(e for e in scene['entities'] if 'terrain' in e['components'])
    terrain['components']['terrain']['sculpt'] = sculpt_id
    for e in scene['entities']:
        if 'camera' in e['components']:
            e['transform']['position'] = list(EYE)
            e['transform']['rotation'] = storm.look_at(EYE, LOOK)
    (PROJECT / SCENE).write_text(json.dumps(scene, indent=2) + '\n', encoding='utf-8')

    doc = dll.noor_doc_open_project(str(PROJECT).encode())
    if not doc:
        sys.exit(f'open project: {dll.noor_last_error().decode()}')
    try:
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
            out = storm.call(dll, 'noor_doc_sculpt', doc, {**base, 'phase': 'dab', 'dabs': dabs})
            storm.call(dll, 'noor_doc_sculpt', doc, {**base, 'phase': 'end'})
            return out

        for _ in range(20):
            lowest = min(height(x, z) for x, z in DABS)
            if lowest <= FLOOR:
                break
            stroke('lower', 8.5, 1.0, 0.75, DABS, 0.25)
        stroke('smooth', 10.0, 0.6, 0.8, DABS, 0.5)
        print(f'lowest dab point {min(height(x, z) for x, z in DABS):.2f} (water at {LEVEL})')
        x0, x1, z0, z1 = RECT
        edge = [(x0 + (x1 - x0) * t / 16, z) for t in range(17) for z in (z0, z1)] + [(x, z0 + (z1 - z0) * t / 16) for t in range(17) for x in (x0, x1)]
        low = min(edge, key=lambda p: height(*p))
        print(f'lowest ground on the rectangle\'s edge {height(*low):.2f} at {low}')
        if height(*low) <= LEVEL + 0.05:
            sys.exit('the water would show a straight edge: move the rectangle or lower the level')

        lake = str(uuid.uuid5(NAMESPACE, 'Lake'))
        storm.execute(dll, doc, {'op': 'create', 'id': lake, 'name': 'Lake',
                                 'transform': {'position': [(x0 + x1) / 2, LEVEL, (z0 + z1) / 2]},
                                 'components': {'water': {'preset': 'Lake', 'size': [x1 - x0, z1 - z0]}}})
        if dll.noor_doc_save_scene(doc) != 1:
            sys.exit(f'save: {dll.noor_last_error().decode()}')
    finally:
        dll.noor_doc_close(doc)
    print('wrote', SCENE, 'and', SCULPT)


if __name__ == '__main__':
    main()
