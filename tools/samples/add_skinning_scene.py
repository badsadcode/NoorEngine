"""The skinning golden scene (Milestone 8C, part 1), built through the editor API like an author would.

    python tools/samples/add_skinning_scene.py

Writes tests/data/render_scenes/Scenes/Skinning.noorscene: in noon light on a floor, three characters placed with
"add_asset" (what dragging a model into the scene does): Ward in his bind pose (T-pose), Ward posed by his joints'
own transforms (arms down, an elbow and a knee bent, head turned), and the CC0 robot (RobotExpressive: rigid parts on
bones, skinned hands) in its file's rest pose, scaled to about Ward's height. Run tools/samples/make_test_characters.py first. Needs a build.
"""
import ctypes
import json
import os
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
PROJECT = ROOT / 'tests' / 'data' / 'render_scenes'
SCENE = 'Scenes/Skinning.noorscene'


def find(name, under):
    found = sorted(under.rglob(name), key=lambda p: p.stat().st_mtime, reverse=True)
    if not found:
        sys.exit(f'{name} not found under {under}: build first')
    return found[0]


def main():
    dll_path = find('noor_editor_api.dll', ROOT / 'build' / 'ninja' / 'tests')
    os.add_dll_directory(str(dll_path.parent))
    dll = ctypes.CDLL(str(dll_path))
    S, P, I = ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int32
    for name, res, args in [('noor_last_error', S, []), ('noor_doc_open_project', P, [S]), ('noor_doc_close', None, [P]),
                            ('noor_doc_new_scene', I, [P, S, S]), ('noor_doc_save_scene', I, [P]), ('noor_doc_execute', I, [P, S]),
                            ('noor_doc_scene', S, [P])]:
        fn = getattr(dll, name)
        fn.restype = res
        fn.argtypes = args

    def run(doc, command):
        if dll.noor_doc_execute(doc, json.dumps(command).encode()) != 1:
            sys.exit(f'{command["op"]}: {dll.noor_last_error().decode()}')

    doc = dll.noor_doc_open_project(str(PROJECT).encode())  # scans: the characters get their ids
    if not doc:
        sys.exit(dll.noor_last_error().decode())
    try:
        guid = {}
        for f in ('Ward_Bind.glb', 'Ward_Posed.glb', 'RobotExpressive.glb'):
            meta = PROJECT / 'Assets' / 'Characters' / (f + '.noormeta')
            guid[f] = json.loads(meta.read_text(encoding='utf-8'))['guid']
        if (PROJECT / SCENE).exists():
            (PROJECT / SCENE).unlink()
        if dll.noor_doc_new_scene(doc, SCENE.encode(), b'Skinning') != 1:
            sys.exit(dll.noor_last_error().decode())
        sprites = json.loads((PROJECT / 'Scenes' / 'Sprites_Sun.noorscene').read_text(encoding='utf-8'))
        for e in sprites['entities']:
            if e['name'] in ('Floor', 'Sun', 'Sky'):
                run(doc, {'op': 'create', 'name': e['name'], 'transform': e['transform'], 'components': e['components']})
        run(doc, {'op': 'create', 'name': 'Camera', 'transform': {'position': [0.1, 1.5, 5.6], 'rotation': [-0.0697565, 0, 0, 0.9975641]},
                  'components': {'camera': {'fov': 50, 'near': 0.05, 'ev100': 15.0, 'tonemapper': 'neutral'}}})
        for f, x, name in (('Ward_Bind.glb', -1.7, 'Ward (bind pose)'), ('Ward_Posed.glb', 0.0, 'Ward (posed)'),
                           ('RobotExpressive.glb', 1.9, 'Robot')):
            run(doc, {'op': 'add_asset', 'asset': guid[f], 'position': [x, 0, 0], 'name': name})
        # The robot is about 4 m tall in its file (its armature scaled x100): brought down to about Ward's height.
        scene = json.loads(dll.noor_doc_scene(doc))
        robot = next(e for e in scene['entities'] if e['name'] == 'Robot')
        run(doc, {'op': 'set_transform', 'entity': robot['id'], 'transform': {'scale': [0.45, 0.45, 0.45]}})
        # The posed Ward turned 35 degrees, so his bent knee and elbow read from the camera.
        posed = next(e for e in scene['entities'] if e['name'] == 'Ward (posed)')
        run(doc, {'op': 'set_transform', 'entity': posed['id'], 'transform': {'rotation': [0, 0.3007058, 0, 0.9537170]}})
        if dll.noor_doc_save_scene(doc) != 1:
            sys.exit(dll.noor_last_error().decode())
        print('wrote', SCENE)

        # What skinning costs: 100 posed Wards in a 10 x 10 crowd, each turned its own way, and the same scene without
        # them (render.skinning_cost measures the difference).
        for path, name, crowd in (('Scenes/Skinning_Empty.noorscene', 'Skinning_Empty', False),
                                  ('Scenes/Skinning_Crowd.noorscene', 'Skinning_Crowd', True)):
            if (PROJECT / path).exists():
                (PROJECT / path).unlink()
            if dll.noor_doc_new_scene(doc, path.encode(), name.encode()) != 1:
                sys.exit(dll.noor_last_error().decode())
            for e in sprites['entities']:
                if e['name'] in ('Floor', 'Sun', 'Sky'):
                    run(doc, {'op': 'create', 'name': e['name'], 'transform': e['transform'], 'components': e['components']})
            run(doc, {'op': 'create', 'name': 'Camera', 'transform': {'position': [0, 7, 16], 'rotation': [-0.2164396, 0, 0, 0.9762960]},
                      'components': {'camera': {'fov': 60, 'near': 0.05, 'ev100': 15.0, 'tonemapper': 'neutral'}}})
            if crowd:
                for k in range(100):
                    run(doc, {'op': 'add_asset', 'asset': guid['Ward_Posed.glb'], 'position': [(k % 10 - 4.5) * 1.6, 0, (k // 10 - 4.5) * 1.6],
                              'name': f'Ward {k}'})
                scene = json.loads(dll.noor_doc_scene(doc))
                import math
                for e in scene['entities']:
                    if e['name'].startswith('Ward ') and e.get('parent') is None:
                        a = math.radians(int(e['name'].split()[1]) * 37 % 360) / 2
                        run(doc, {'op': 'set_transform', 'entity': e['id'], 'transform': {'rotation': [0, math.sin(a), 0, math.cos(a)]}})
            if dll.noor_doc_save_scene(doc) != 1:
                sys.exit(dll.noor_last_error().decode())
            print('wrote', path)
    finally:
        dll.noor_doc_close(doc)


if __name__ == '__main__':
    main()
