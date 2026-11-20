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
Part 3 (blending and the controller, Ward.nooranim):
  tests/data/render_scenes/Scenes/Blending.noorscene         the golden sequence: four Wards on one controller with
      different parameters: between idle and walk, between walk and run, walking and waving (a masked layer), running
      and leaning (an additive layer). render.golden_Blending_<frame> captures three moments.
  tests/data/render_scenes/Scenes/Blending_Crowd.noorscene   100 Wards on the controller, speeds from idle to run, half
      of them waving, all leaning a little (render.animation_cost).
  tests/data/animation_scripts/Scenes/Ward_Controller(_CSharp).noorscene  a script speeds Ward up from standing to a
      run, triggers a nod, waves, and leans on when the nod has ended: in Lua and in C#.

Part 4 (root motion, events, props on bones):
  tests/data/render_scenes/Scenes/Root_Motion.noorscene      the golden: Ward walking across on root motion with a
      sword in his right hand (an entity parented to the hand's bone), and Ward inside a character controller walking
      into a wall that stops him.
  tests/data/animation_scripts/Scenes/Ward_Walk(_CSharp).noorscene  Ward in a character controller on root motion,
      hearing his footsteps (animation events), walking faster after four of them, until a wall stops him.

    python tools/samples/add_animation_scene.py [part2] [part3] [part4]   (all when none is named)
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
        for f in ('Ward_Anim.glb', 'RobotExpressive.glb', 'Ward.nooranim'):
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

        parts = set(sys.argv[1:]) or {'part2', 'part3', 'part4'}

        def subtree_entity(root, name):
            scene = json.loads(dll.noor_doc_scene(doc))['entities']
            below = {root}
            for e in scene:
                if e.get('parent') in below:
                    if e['name'] == name:
                        return e['id']
                    below.add(e['id'])
            sys.exit(f'no {name} under {root}')

        def ground():
            run(doc, {'op': 'create', 'name': 'Ground (collision)', 'transform': {'position': [0, -0.5, 0]},
                      'components': {'collider': {'shape': 'box', 'size': [60, 1, 60]}}})

        def character_ward(name, feet, turn, animator):
            # A player-like entity with a character controller, Ward inside it (as Create > Player (third person) does).
            pid = name + ' (player)'
            run(doc, {'op': 'create', 'name': pid, 'transform': {'position': feet, 'rotation': yaw(turn)},
                      'components': {'character': {'radius': 0.3, 'height': 1.8, 'step_height': 0.4}}})
            player = entity(doc, pid)['id']
            run(doc, {'op': 'add_asset', 'asset': guid['Ward_Anim.glb'], 'position': feet, 'name': name})
            ward = entity(doc, name)['id']
            run(doc, {'op': 'reparent', 'entity': ward, 'parent': player})
            run(doc, {'op': 'set_transform', 'entity': ward, 'transform': {'position': [0, 0, 0], 'rotation': [0, 0, 0, 1]}})
            run(doc, {'op': 'set_component', 'entity': ward, 'type': 'animator', 'value': animator})
            return ward

        def wall(name, position, size):
            run(doc, {'op': 'create', 'name': name, 'transform': {'position': position},
                      'components': {'primitive': {'shape': 'box', 'size': size, 'color': '#9A8F80'},
                                     'collider': {'shape': 'box', 'size': size}}})

        if 'part4' in parts:
            new_scene('Scenes/Root_Motion.noorscene', 'Root_Motion', {'position': [0, 2.2, 8.5], 'rotation': [-0.1305262, 0, 0, 0.9914449]})
            ground()
            sword_ward = place('Ward_Anim.glb', 'Ward (sword)', -3.0, 0.8, turn=90, animator={'animation': 'WalkForward', 'root_motion': True})
            hand = subtree_entity(sword_ward, 'DEF-hand.R')
            run(doc, {'op': 'create', 'name': 'Sword', 'parent': hand, 'transform': {'position': [-0.05, 0, 0.35]},
                      'components': {'primitive': {'shape': 'box', 'size': [0.03, 0.07, 0.8], 'color': '#C8D0D8'}}})
            character_ward('Ward (wall)', [1.0, 0.05, -1.2], 90, {'animation': 'WalkForward', 'root_motion': True})
            wall('Wall', [3.3, 1, -1.2], [0.2, 2, 2.5])
            save('Scenes/Root_Motion.noorscene')
            (SCRIPTS / 'Scenes').mkdir(parents=True, exist_ok=True)
            for name, script in (('Ward_Walk', 'Scripts/ward_walk.luau'), ('Ward_Walk_CSharp', 'Scripts/WardWalk.cs')):
                path = f'Scenes/{name}.noorscene'
                new_scene(path, name, {'position': [4, 2, 2], 'rotation': [-0.0868241, 0.3420201, 0.0318190, 0.9351131]})
                ground()
                ward = character_ward('Ward', [0, 0.05, 0], 0, {
                    'animation': 'WalkForward', 'root_motion': True,
                    'events': [{'clip': 'WalkForward', 'at': 0.25, 'name': 'left'}, {'clip': 'WalkForward', 'at': 0.75, 'name': 'right'}]})
                run(doc, {'op': 'set_component', 'entity': ward, 'type': 'script', 'value': {'file': script}})
                wall('Wall', [0, 1, 5.1], [3, 2, 0.2])
                save(path)
                shutil.move(PROJECT / path, SCRIPTS / path)

        def scripted(name, script, x, animator):
            path = f'Scenes/{name}.noorscene'
            new_scene(path, name, {'position': [0, 1.4, 5.0], 'rotation': [-0.0697565, 0, 0, 0.9975641]})
            root = place('Ward_Anim.glb', 'Ward', x, animator=animator)
            run(doc, {'op': 'set_component', 'entity': root, 'type': 'script', 'value': {'file': script}})
            save(path)
            shutil.move(PROJECT / path, SCRIPTS / path)

        if 'part3' in parts:
            ctrl = guid['Ward.nooranim']
            new_scene('Scenes/Blending.noorscene', 'Blending', {'position': [0.1, 1.5, 6.2], 'rotation': [-0.0697565, 0, 0, 0.9975641]})
            for name, x, turn, params in (('Ward (idle to walk)', -2.4, 30, {'speed': 0.7}), ('Ward (walk to run)', -0.8, 30, {'speed': 2.7}),
                                          ('Ward (walking, waving)', 0.8, 0, {'speed': 1.4, 'waving': True}),
                                          ('Ward (running, leaning)', 2.4, -30, {'speed': 4, 'lean': 1})):
                place('Ward_Anim.glb', name, x, turn=turn, animator={'controller': ctrl, 'parameters': params})
            save('Scenes/Blending.noorscene')
            new_scene('Scenes/Blending_Crowd.noorscene', 'Blending_Crowd',
                      {'position': [0, 7, 16], 'rotation': [-0.2164396, 0, 0, 0.9762960]})
            for k in range(100):
                place('Ward_Anim.glb', f'Ward {k}', (k % 10 - 4.5) * 1.6, (k // 10 - 4.5) * 1.6, turn=k * 37 % 360,
                      animator={'controller': ctrl, 'parameters': {'speed': round(k * 0.04, 2), 'waving': k % 2 == 0, 'lean': 0.3}})
            save('Scenes/Blending_Crowd.noorscene')
            (SCRIPTS / 'Scenes').mkdir(parents=True, exist_ok=True)
            for name, script, x in (('Ward_Controller', 'Scripts/ward_controller.luau', -0.9),
                                    ('Ward_Controller_CSharp', 'Scripts/WardController.cs', 0.9)):
                scripted(name, script, x, {'controller': ctrl})
        if 'part2' not in parts:
            return

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
            scripted(name, script, x, {'animation': 'Walk'})
    finally:
        dll.noor_doc_close(doc)


if __name__ == '__main__':
    main()
