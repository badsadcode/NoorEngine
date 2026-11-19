"""Hillside under every built-in environment preset, and on an airless world (Milestone 8A, part 8: the 8A sample).

    python tools/samples/add_hillside_presets.py

Writes samples/Hillside/Scenes/Presets/:
  - Hillside_<Preset>.noorscene for each preset the engine ships (noor_environment_presets): the Hillside scene with
    its camera tilted up 8 degrees so the sky shows, its environment set by **applying the preset through the editor
    API** ({"op": "apply"} on noor_doc_environment), which is what the editor's Environments gallery does: the preset is
    copied into Assets/Environments/<Preset>.noorenv and the scene points at it. One golden render each.
  - Hillside_Space.noorscene: the same land, house and forest on an airless world ("sky": "space"): a black sky with
    stars, a ringed planet and a small moon over the valley, and a second, orange sun. Golden render too.

Reads samples/Hillside/Scenes/Hillside.noorscene (build_hillside.py, add_hillside_forest.py). Run it again after
rebuilding Hillside or changing a preset. Needs a build (cmake --build --preset ninja-release).
"""
import ctypes
import json
import math
import os
import pathlib
import sys
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[2]
PROJECT = ROOT / 'samples' / 'Hillside'
OUT = PROJECT / 'Scenes' / 'Presets'
NAMESPACE = uuid.UUID('6f1d0c3a-8b2e-4f57-a9c4-3e7b2d5a8c10')
TILT_DEG = 8.0


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
                            ('noor_environment_presets', S, []), ('noor_doc_open_project', P, [S]), ('noor_doc_close', None, [P]),
                            ('noor_doc_open_scene', I, [P, S]), ('noor_doc_save_scene', I, [P]), ('noor_doc_environment', S, [P, S])]:
        fn = getattr(dll, name)
        fn.restype = res
        fn.argtypes = args
    if dll.noor_editor_api_version() < 24:
        sys.exit('noor_editor_api.dll is older than v24 (environment assets): rebuild')
    return dll


# ---- small quaternion helpers (x, y, z, w; the engine's order) ----

def q_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz]


def q_axis(axis, degrees):
    h = math.radians(degrees) * 0.5
    return [axis[0] * math.sin(h), axis[1] * math.sin(h), axis[2] * math.sin(h), math.cos(h)]


def q_rotate(q, v):
    p = q_mul(q_mul(q, [v[0], v[1], v[2], 0.0]), [-q[0], -q[1], -q[2], q[3]])
    return p[:3]


def q_looking(forward):
    """A rotation whose forward (-Z) points along `forward` (yaw, then pitch; no roll)."""
    x, y, z = forward
    yaw = math.degrees(math.atan2(-x, -z))
    pitch = math.degrees(math.asin(max(-1.0, min(1.0, y / math.sqrt(x * x + y * y + z * z)))))
    return q_mul(q_axis((0, 1, 0), yaw), q_axis((1, 0, 0), pitch))


def direction(yaw_deg, up_deg):
    """Unit vector: yaw about +Y from -Z (positive turns left), raised `up_deg` above the horizon."""
    yaw, up = math.radians(yaw_deg), math.radians(up_deg)
    return [-math.sin(yaw) * math.cos(up), math.sin(up), -math.cos(yaw) * math.cos(up)]


def scene_variant(base, name):
    scene = json.loads(json.dumps(base))
    scene['name'] = name
    scene['id'] = str(uuid.uuid5(NAMESPACE, name))
    for e in scene['entities']:
        if 'camera' in e['components']:
            e['transform']['rotation'] = q_mul(e['transform']['rotation'], q_axis((1, 0, 0), TILT_DEG))
    return scene


def camera_yaw(scene):
    for e in scene['entities']:
        if 'camera' in e['components']:
            f = q_rotate(e['transform']['rotation'], [0.0, 0.0, -1.0])
            return math.degrees(math.atan2(-f[0], -f[2]))
    return 0.0


def space_scene(base):
    """Hillside on an airless world: stars, a ringed planet and a moon over the valley, a second orange sun."""
    scene = scene_variant(base, 'Hillside_Space')
    yaw = camera_yaw(scene)
    for e in scene['entities']:
        if 'environment' in e['components']:
            e['components']['environment'] = {'sky': 'space', 'stars': True, 'star_brightness': 100000, 'background': True,
                                              'ev100': 14.5}
        light = e['components'].get('light')
        if light and light.get('type', 'directional') == 'directional':
            # The main sun low behind the camera's left shoulder: long shadows across the valley.
            e['transform']['rotation'] = q_looking([-v for v in direction(yaw + 140.0, 14.0)])
            light['illuminance'] = 100000
            light['color'] = '#FFFFFF'

    def entity(name, components, rotation):
        return {'id': str(uuid.uuid5(NAMESPACE, 'Hillside_Space/' + name)), 'name': name, 'layer': 'default', 'tags': [],
                'parent': None, 'transform': {'position': [0, 0, 0], 'rotation': rotation, 'scale': [1, 1, 1]},
                'components': components}

    scene['entities'] += [
        entity('Ringed planet', {'celestial_body': {'angular_size': 18, 'color': '#8FA8C8',
                                                    'rings': {'inner': 1.35, 'outer': 2.3, 'color': '#D8C8A8', 'tilt': 24}}},
               q_looking(direction(yaw - 16.0, 17.0))),  # right of the house, clear of the oak
        entity('Small moon', {'celestial_body': {'angular_size': 2.5, 'color': '#B0A898'}}, q_looking(direction(yaw + 6.0, 21.0))),  # above the house
        entity('Second sun', {'light': {'type': 'directional', 'color': '#FFB070', 'illuminance': 30000}},
               q_looking([-v for v in direction(yaw - 120.0, 9.0)])),
    ]
    return scene


def main():
    dll = load_api()
    presets = json.loads(dll.noor_environment_presets().decode('utf-8'))
    base = json.loads((PROJECT / 'Scenes' / 'Hillside.noorscene').read_text(encoding='utf-8'))
    OUT.mkdir(parents=True, exist_ok=True)
    names = []
    for preset in presets:
        name = 'Hillside_' + preset['name'].replace(' ', '')
        (OUT / f'{name}.noorscene').write_text(json.dumps(scene_variant(base, name), indent=2) + '\n', encoding='utf-8')
        names.append((name, preset['name']))
    (OUT / 'Hillside_Space.noorscene').write_text(json.dumps(space_scene(base), indent=2) + '\n', encoding='utf-8')

    # Apply each preset the way the editor does (the project's scan picks the new scenes up on open).
    doc = dll.noor_doc_open_project(str(PROJECT).encode('utf-8'))
    if not doc:
        sys.exit('open project failed: ' + (dll.noor_last_error() or b'').decode())
    try:
        for name, preset in names:
            if dll.noor_doc_open_scene(doc, f'Scenes/Presets/{name}.noorscene'.encode()) != 1:
                sys.exit(f'open {name} failed: ' + (dll.noor_last_error() or b'').decode())
            if not dll.noor_doc_environment(doc, json.dumps({'op': 'apply', 'preset': preset}).encode('utf-8')):
                sys.exit(f'apply {preset} failed: ' + (dll.noor_last_error() or b'').decode())
            if dll.noor_doc_save_scene(doc) != 1:
                sys.exit(f'save {name} failed: ' + (dll.noor_last_error() or b'').decode())
            print('wrote', f'Scenes/Presets/{name}.noorscene', '->', f'Assets/Environments/{preset}.noorenv')
    finally:
        dll.noor_doc_close(doc)
    print('wrote Scenes/Presets/Hillside_Space.noorscene')


if __name__ == '__main__':
    main()
