"""The animation scenes (Milestone 8C, part 2), built through the editor API like an author would.

    python tools/samples/add_animation_scene.py

Run tools/samples/make_test_characters.py first (Ward_Anim.glb). Needs a build. Writes:
  tests/data/render_scenes/Scenes/Animation.noorscene        the golden: three Wards and the CC0 robot, each playing a
      clip on the scene's clock (no scripts): Ward walking, Ward waving (once: held at the end), Ward nodding, and the
      robot walking. render.golden_Animation captures it at a fixed frame, so every bone is at a known time.
  tests/data/render_scenes/Scenes/Animation_Crowd.noorscene  100 walking Wards, each a little further through his
      walk (time_offset) and turned his own way: what animation costs (render.animation_cost).
  tests/data/animation_scripts/Scenes/Ward_Script(.CSharp).noorscene  Ward walking until a script waves (a one-shot,
      at double speed), nods when the wave ends, and notes when the nod ends: in Lua and in C#. The built-game check
      (cli.animation_scripts_build_identically) runs them in the project and in a built game.
"""
import ctypes
import json
import math
import os
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
PROJECT = ROOT / 'tests' / 'data' / 'render_scenes'
SCRIPTS = ROOT / 'tests' / 'data' / 'animation_scripts'


def find(name, under):
    found = sorted(under.rglob(name), key=lambda p: p.stat().st_mtime, reverse=True)
    if not found:
        sys.exit(f'{name} not found under {under}: build first')
    return found[0]


def yaw(degrees):
    a = math.radians(degrees) / 2
    return [0, math.sin(a), 0, math.cos(a)]


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

    def entity(doc, name):
        return next(e for e in json.loads(dll.noor_doc_scene(doc))['entities'] if e['name'] == name)

    doc = dll.noor_doc_open_project(str(PROJECT).encode())  # scans: Ward_Anim gets its id
    if not doc:
        sys.exit(dll.noor_last_error().decode())
    try:
        guid = {}
        for f in ('Ward_Anim.glb', 'RobotExpressive.glb'):
            guid[f] = json.loads((PROJECT / 'Assets' / 'Characters' / (f + '.noormeta')).read_text(encoding='utf-8'))['guid']
        sprites = json.loads((PROJECT / 'Scenes' / 'Sprites_Sun.noorscene').read_text(encoding='utf-8'))

        def new_scene(path, name, camera):
            if (PROJECT / path).exists():
                (PROJECT / path).unlink()
            if dll.noor_doc_new_scene(doc, path.encode(), name.encode()) != 1:
                sys.exit(dll.noor_last_error().decode())
            for e in sprites['entities']:
                if e['name'] in ('Floor', 'Sun', 'Sky'):
                    run(doc, {'op': 'create', 'name': e['name'], 'transform': e['transform'], 'components': e['components']})
            run(doc, {'op': 'create', 'name': 'Camera', 'transform': camera,
                      'components': {'camera': {'fov': 50, 'near': 0.05, 'ev100': 15.0, 'tonemapper': 'neutral'}}})

        def save(path):
            if dll.noor_doc_save_scene(doc) != 1:
                sys.exit(dll.noor_last_error().decode())
            print('wrote', path)

        def place(asset, name, x, z=0.0, turn=0.0, animator=None, scale=None):
            run(doc, {'op': 'add_asset', 'asset': guid[asset], 'position': [x, 0, z], 'name': name})
            e = entity(doc, name)
            transform = {'rotation': yaw(turn)}
            if scale:
                transform['scale'] = [scale] * 3
            run(doc, {'op': 'set_transform', 'entity': e['id'], 'transform': transform})
            if animator:
                run(doc, {'op': 'set_component', 'entity': e['id'], 'type': 'animator', 'value': animator})
            return e['id']

        # The golden: four characters, each on the scene's clock.
        new_scene('Scenes/Animation.noorscene', 'Animation', {'position': [0.1, 1.5, 6.2], 'rotation': [-0.0697565, 0, 0, 0.9975641]})
        place('Ward_Anim.glb', 'Ward (walking)', -2.4, turn=35, animator={'animation': 'Walk'})
        place('Ward_Anim.glb', 'Ward (waving)', -0.8, animator={'animation': 'Wave', 'loop': False})
        place('Ward_Anim.glb', 'Ward (nodding)', 0.8, turn=-20, animator={'animation': 'Nod', 'loop': False})
        # The robot is about 4 m tall in its file: brought down to about Ward's height, walking towards the camera.
        place('RobotExpressive.glb', 'Robot (walking)', 2.5, animator={'animation': 'Walking', 'time_offset': 0.2}, scale=0.45)
        save('Scenes/Animation.noorscene')

        # The cost: 100 walking Wards.
        new_scene('Scenes/Animation_Crowd.noorscene', 'Animation_Crowd',
                  {'position': [0, 7, 16], 'rotation': [-0.2164396, 0, 0, 0.9762960]})
        for k in range(100):
            place('Ward_Anim.glb', f'Ward {k}', (k % 10 - 4.5) * 1.6, (k // 10 - 4.5) * 1.6, turn=k * 37 % 360,
                  animator={'animation': 'Walk', 'time_offset': round(k * 0.137 % 1.0, 3)})
        save('Scenes/Animation_Crowd.noorscene')

        # The scripted ones: made here (where Ward_Anim's id is known), kept beside their scripts.
        (SCRIPTS / 'Scenes').mkdir(parents=True, exist_ok=True)
        for name, script, x in (('Ward_Script', 'Scripts/ward_wave.luau', -0.9), ('Ward_Script_CSharp', 'Scripts/WardWave.cs', 0.9)):
            path = f'Scenes/{name}.noorscene'
            new_scene(path, name, {'position': [0, 1.4, 5.0], 'rotation': [-0.0697565, 0, 0, 0.9975641]})
            root = place('Ward_Anim.glb', 'Ward', x, animator={'animation': 'Walk'})
            run(doc, {'op': 'set_component', 'entity': root, 'type': 'script', 'value': {'file': script}})
            save(path)
            shutil.move(PROJECT / path, SCRIPTS / path)
    finally:
        dll.noor_doc_close(doc)


if __name__ == '__main__':
    main()
