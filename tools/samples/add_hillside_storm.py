"""A stormy evening on Hillside (Milestone 8B, part 8: the 8B sample).

    python tools/samples/add_hillside_storm.py

Writes samples/Hillside/Scenes/Hillside_Storm.noorscene: the valley at dusk (the Dusk preset), and then a storm rolls
in over four seconds (a weather zone a script fades in: rain, a strong wind, low cloud, fog). By the house a campfire
burns: the Fire, Smoke and Sparks presets **applied through the editor API**, as the Effects panel's Add to scene does,
the sparks colliding with the hillside, and a lamp for its glow. Coth, from The Uncounted, walks round the fire: an
8-direction sprite on a character controller, so he follows the ground and turns with the camera.

Everything is added through the editor API (noor_doc_effect, noor_doc_sprite, noor_doc_execute), like the 8A sample.
The ground heights the fire stands on are measured first: a throwaway probe scene raycasts down at those points in
noor_player and logs the hits.

Reads Scenes/Presets/Hillside_Dusk.noorscene (add_hillside_presets.py) and Coth's sheet from the test data
(tests/data/render_scenes/Assets/Sprites/coth.png: Tom's art from The Uncounted, published with his permission).
Needs a build (cmake --build --preset ninja-release).
"""
import ctypes
import json
import math
import os
import pathlib
import re
import shutil
import subprocess
import sys
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[2]
PROJECT = ROOT / 'samples' / 'Hillside'
NAMESPACE = uuid.UUID('8b0c1d2e-3f40-4152-a637-48596a7b8c9d')
SCENE = 'Scenes/Hillside_Storm.noorscene'
FIRE_XZ = (27.0, -13.0)  # by the house, in front of the camera
FIRE_EMISSION = 600
SPARKS_EMISSION = 3000
TIME_OF_DAY = 19.2
COTH_START_DEG = -167.0
COTH_SHEET = {
    'grid': {'columns': 8, 'rows': 6, 'margin': 0, 'spacing': 0}, 'directions': 8, 'pivot': [0.5, 10.0 / 128.0],
    'pixels_per_metre': 64, 'filter': 'linear',
    'animations': {'idle': {'frames': [0], 'fps': 2, 'loop': True},
                   'walk': {'frames': [8, 16], 'fps': 6, 'loop': True},
                   'windup': {'frames': [24], 'fps': 4, 'loop': False},
                   'attack': {'frames': [32], 'fps': 6, 'loop': False},
                   'hurt': {'frames': [40], 'fps': 4, 'loop': False, 'directional': False},
                   'die': {'frames': [40, 44], 'fps': 3, 'loop': False, 'directional': False}},
}
STORM = {'rain': 0.8, 'wind_speed': 7, 'wind_direction': 60,
         'cloud_coverage': 0.75, 'cloud_density': 40, 'cloud_altitude': 900, 'cloud_scale': 1800, 'cloud_wind_speed': 18,
         'cloud_wind_direction': 70, 'cloud_seed': 7, 'fog_density': 0.006, 'fog_height': 2, 'fog_color': '#9AA3AE'}


def find(name, under):
    found = sorted(under.rglob(name), key=lambda p: p.stat().st_mtime, reverse=True)
    if not found:
        sys.exit(f'{name} not found under {under}: build first')
    return found[0]


def load_api():
    dll_path = find('noor_editor_api.dll', ROOT / 'build' / 'ninja' / 'tests')
    os.add_dll_directory(str(dll_path.parent))
    dll = ctypes.CDLL(str(dll_path))
    S, P, I = ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int32
    for name, res, args in [('noor_editor_api_version', ctypes.c_uint32, []), ('noor_last_error', S, []),
                            ('noor_doc_open_project', P, [S]), ('noor_doc_close', None, [P]), ('noor_doc_open_scene', I, [P, S]),
                            ('noor_doc_save_scene', I, [P]), ('noor_doc_execute', I, [P, S]), ('noor_doc_effect', S, [P, S]),
                            ('noor_doc_sprite', S, [P, S])]:
        fn = getattr(dll, name)
        fn.restype = res
        fn.argtypes = args
    if dll.noor_editor_api_version() < 26:
        sys.exit('noor_editor_api.dll is older than v26 (sprites): rebuild')
    return dll


def call(dll, fn, doc, command):
    out = getattr(dll, fn)(doc, json.dumps(command).encode())
    if not out:
        sys.exit(f'{fn} {command.get("op")}: {dll.noor_last_error().decode()}')
    return json.loads(out)


def execute(dll, doc, command):
    if dll.noor_doc_execute(doc, json.dumps(command).encode()) != 1:
        sys.exit(f'{command.get("op")}: {dll.noor_last_error().decode()}')


def look_at(eye, target):
    """The rotation (x, y, z, w) that points an entity's -Z from `eye` at `target` (yaw about Y, then pitch about X)."""
    dx, dy, dz = (t - e for t, e in zip(target, eye))
    yaw = math.atan2(-dx, -dz)
    pitch = math.atan2(dy, math.hypot(dx, dz))
    qy = (0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2))
    qx = (math.sin(pitch / 2), 0.0, 0.0, math.cos(pitch / 2))
    ax, ay, az, aw = qy
    bx, by, bz, bw = qx
    return [round(v, 9) for v in (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
                                  aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)]


def ground_heights(points):
    """Raycasts down at (x, z) points in a throwaway scene in noor_player; returns their ground heights."""
    player = find('noor_player.exe', ROOT / 'build' / 'ninja' / 'tools' / 'noor_player')
    base = json.loads((PROJECT / 'Scenes' / 'Presets' / 'Hillside_Dusk.noorscene').read_text(encoding='utf-8'))
    probe = PROJECT / 'Scripts' / '_probe.luau'
    probe.write_text('''local P = {}
function P:on_update()
    if self.done then return end
    self.done = true
    for _, xz in ipairs(self.params.points) do
        local hit = noor.raycast(vector.create(xz[1], 300, xz[2]), vector.create(0, -1, 0), 600)
        noor.log("PROBE", xz[1], xz[2], hit and hit.point.y or "none")
    end
end
return P
''', encoding='utf-8')
    base['entities'].append({'id': str(uuid.uuid5(NAMESPACE, 'probe')), 'name': 'Probe', 'layer': 'default', 'tags': [], 'parent': None,
                             'transform': {'position': [0, 0, 0], 'rotation': [0, 0, 0, 1], 'scale': [1, 1, 1]},
                             'components': {'script': {'file': 'Scripts/_probe.luau', 'params': {'points': [list(p) for p in points]}}}})
    scene = PROJECT / 'Scenes' / '_probe.noorscene'
    scene.write_text(json.dumps(base), encoding='utf-8')
    log = PROJECT / '.noor' / 'probe.log'
    try:
        subprocess.run([str(player), str(PROJECT), '--scene', 'Scenes/_probe.noorscene', '--frames', '5', '--no-audio', '--no-input',
                        '--log', str(log)], check=False, capture_output=True, timeout=300)
        text = log.read_text(encoding='utf-8', errors='replace')
    finally:
        scene.unlink(missing_ok=True)
        probe.unlink(missing_ok=True)
    heights = {}
    for m in re.finditer(r'PROBE\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+|none)', text):
        if m.group(3) != 'none':
            heights[(float(m.group(1)), float(m.group(2)))] = float(m.group(3))
    missing = [p for p in points if p not in heights]
    if missing:
        sys.exit(f'no ground under {missing} (see {log})')
    return heights


def main():
    sprites = PROJECT / 'Assets' / 'Sprites'
    sprites.mkdir(exist_ok=True)
    shutil.copyfile(ROOT / 'tests' / 'data' / 'render_scenes' / 'Assets' / 'Sprites' / 'coth.png', sprites / 'coth.png')

    fire_y = ground_heights([FIRE_XZ])[FIRE_XZ]
    fx, fz = FIRE_XZ
    print(f'the fire stands at y = {fire_y:.3f}')

    # The scene: Dusk, seen from down the slope, looking at the fire with the house behind it.
    scene = json.loads((PROJECT / 'Scenes' / 'Presets' / 'Hillside_Dusk.noorscene').read_text(encoding='utf-8'))
    scene['name'] = 'Hillside_Storm'
    scene['id'] = str(uuid.uuid5(NAMESPACE, 'Hillside_Storm'))
    eye = [fx + 4.6, fire_y + 2.4, fz - 6.2]
    for e in scene['entities']:
        if 'camera' in e['components']:
            e['transform']['position'] = [round(v, 4) for v in eye]
            e['transform']['rotation'] = look_at(eye, [fx - 1.0, fire_y + 1.0, fz + 1.0])
    (PROJECT / SCENE).write_text(json.dumps(scene, indent=2) + '\n', encoding='utf-8')

    dll = load_api()
    doc = dll.noor_doc_open_project(str(PROJECT).encode())
    if not doc:
        sys.exit(f'open project: {dll.noor_last_error().decode()}')
    try:
        if dll.noor_doc_open_scene(doc, SCENE.encode()) != 1:
            sys.exit(f'open scene: {dll.noor_last_error().decode()}')
        # The campfire: three presets, applied as the Effects panel does (each copied into Assets/Effects once).
        applied = {}
        for preset, dy in (('Fire', 0.05), ('Smoke', 0.6), ('Sparks', 0.15)):
            applied[preset] = call(dll, 'noor_doc_effect', doc, {'op': 'apply', 'preset': preset, 'position': [fx, fire_y + dy, fz]})
        # The sparks land on the hillside and bounce (part 7): an override on the entity, the effect file untouched.
        sparks = applied['Sparks']
        execute(dll, doc, {'op': 'set_component', 'entity': sparks['entity'], 'type': 'emitter',
                           'value': {'asset': sparks['guid'], 'collide': {'bounce': 0.35, 'friction': 0.4, 'radius': 0.01},
                                     'render': {'blend': 'additive', 'facing': 'velocity', 'stretch': 0.04, 'emission': SPARKS_EMISSION}}})
        # Automatic exposure opens up for a dark evening and doesn't see the fire's own light (8B part 2), so the
        # glow the presets suit to noon is turned down here, as the night campfire test does.
        execute(dll, doc, {'op': 'set_component', 'entity': applied['Fire']['entity'], 'type': 'emitter',
                           'value': {'asset': applied['Fire']['guid'], 'render': {'blend': 'additive', 'emission': FIRE_EMISSION}}})
        # A little earlier in the evening than the Dusk preset, so the valley reads under the storm.
        for e in scene['entities']:
            if 'environment' in e['components']:
                execute(dll, doc, {'op': 'set_component', 'entity': e['id'], 'type': 'environment',
                                   'value': dict(e['components']['environment'], time_of_day=TIME_OF_DAY)})
        execute(dll, doc, {'op': 'create', 'id': str(uuid.uuid5(NAMESPACE, 'Firelight')), 'name': 'Firelight',
                           'transform': {'position': [fx, fire_y + 0.8, fz]},
                           'components': {'light': {'type': 'point', 'color': '#FFB060', 'intensity': 1600, 'range': 9}}})

        # Coth's sheet, made and sliced through the API (as Make sprite sheet and the sheet editor do).
        if (PROJECT / 'Assets' / 'Sprites' / 'coth.noorsprite').exists():  # made on an earlier run: kept, set again
            made = call(dll, 'noor_doc_sprite', doc, {'op': 'read', 'asset': 'Assets/Sprites/coth.noorsprite'})
        else:
            made = call(dll, 'noor_doc_sprite', doc, {'op': 'create', 'texture': 'Assets/Sprites/coth.png', 'name': 'coth'})
        sheet = dict(made['sheet'], name='Coth', **COTH_SHEET)
        saved = call(dll, 'noor_doc_sprite', doc, {'op': 'save', 'asset': made['path'], 'sheet': sheet})
        if 'error' in saved:
            sys.exit(f'Coth\'s sheet: {saved["error"]}')
        coth = str(uuid.uuid5(NAMESPACE, 'Coth'))
        # He starts where, walking anticlockwise at 1.4 m/s, he's on the camera's side of the fire at the golden's
        # moment (5.5 s in), lit by it.
        start = math.radians(COTH_START_DEG)
        execute(dll, doc, {'op': 'create', 'id': coth, 'name': 'Coth',
                           'transform': {'position': [fx + 3.0 * math.cos(start), fire_y + 1.5, fz + 3.0 * math.sin(start)]},
                           'components': {'character': {'radius': 0.3, 'height': 1.75, 'step_height': 0.35, 'max_slope': 50},
                                          'script': {'file': 'Scripts/coth_circle.luau',
                                                     'params': {'centre_x': fx, 'centre_z': fz, 'radius': 3.0, 'speed': 1.4}}}})
        # The picture on a child, its feet on the character's (a character's origin is at its feet).
        execute(dll, doc, {'op': 'create', 'id': str(uuid.uuid5(NAMESPACE, 'Coth sprite')), 'name': 'Coth sprite', 'parent': coth,
                           'transform': {'position': [0, 0, 0]},
                           'components': {'sprite': {'sheet': saved['guid'], 'animation': 'idle'}}})

        # The storm: a zone over the whole valley whose weight a script rolls from 0 to 1.
        execute(dll, doc, {'op': 'create', 'id': str(uuid.uuid5(NAMESPACE, 'Storm')), 'name': 'Storm',
                           'transform': {'position': [0, 0, 0]},
                           'components': {'weather_zone': {'shape': 'box', 'size': [600, 400, 600], 'blend': 1, 'priority': 0, 'weight': 0,
                                                           'environment': STORM},
                                          'script': {'file': 'Scripts/storm_roll.luau', 'params': {'seconds': 4}}}})
        if dll.noor_doc_save_scene(doc) != 1:
            sys.exit(f'save: {dll.noor_last_error().decode()}')
    finally:
        dll.noor_doc_close(doc)
    print('wrote', SCENE)


if __name__ == '__main__':
    main()
