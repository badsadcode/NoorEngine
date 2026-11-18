"""Builds samples/Hillside with NoorEngine's own editor tools (Milestone 6, part 8).

Every step goes through the editor API (noor_editor_api.dll, the same C functions NoorEditor
calls): materials from texture sets, a terrain, sculpt and paint strokes, CSG brushes, saving.
Nothing is written by hand. Run it again to rebuild the sample from scratch:

    python tools/samples/build_hillside.py [path/to/noor_editor_api.dll]

The DLL defaults to build/ninja/tests/RelWithDebInfo (then Debug). The textures in
samples/Hillside/Assets/Textures and the environment are inputs; everything else is rebuilt.
"""
import ctypes, json, math, os, shutil, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PROJECT = os.path.join(ROOT, 'samples', 'Hillside')
SCENE = 'Scenes/Hillside.noorscene'


def load_api(path=None):
    candidates = [path] if path else [os.path.join(ROOT, 'build', 'ninja', 'tests', c, 'noor_editor_api.dll') for c in ('RelWithDebInfo', 'Debug')]
    for p in candidates:
        if p and os.path.exists(p):
            p = os.path.abspath(p)
            os.add_dll_directory(os.path.dirname(p))
            dll = ctypes.CDLL(p)
            break
    else:
        sys.exit('noor_editor_api.dll not found: build first (cmake --build --preset ninja-release)')
    S, P, I, F = ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int32, ctypes.c_float
    for name, res, args in [
        ('noor_editor_api_version', ctypes.c_uint32, []), ('noor_last_error', S, []),
        ('noor_doc_open_project', P, [S]), ('noor_doc_close', None, [P]), ('noor_doc_assets', S, [P, I]),
        ('noor_doc_make_material', S, [P, S]), ('noor_doc_new_scene', I, [P, S, S]), ('noor_doc_scene', S, [P]),
        ('noor_doc_execute', I, [P, S]), ('noor_doc_sculpt', S, [P, S]), ('noor_doc_save_scene', I, [P]),
        ('noor_doc_terrain_height', I, [P, S, F, F, ctypes.POINTER(F)]),
    ]:
        fn = getattr(dll, name)
        fn.restype, fn.argtypes = res, args
    if dll.noor_editor_api_version() < 19:
        sys.exit('this script needs editor API v19 or newer (noor_doc_terrain_height)')
    return dll


class Doc:
    def __init__(self, api, project):
        self.api = api
        self.doc = api.noor_doc_open_project(project.encode())
        if not self.doc:
            self.fail('open the project')

    def fail(self, what):
        sys.exit(f'could not {what}: {self.api.noor_last_error().decode()}')

    def assets(self):
        return json.loads(self.api.noor_doc_assets(self.doc, 1))

    def guid(self, path):
        for a in self.assets():
            if a['path'] == path:
                return a['guid']
        self.fail(f'find asset {path}')

    def execute(self, command):
        if self.api.noor_doc_execute(self.doc, json.dumps(command).encode()) != 1:
            self.fail(f"run {command['op']}")

    def create(self, name, components, position=(0, 0, 0), parent=None, rotation=(0, 0, 0, 1)):
        command = {'op': 'create', 'name': name, 'components': components,
                   'transform': {'position': list(position), 'rotation': list(rotation)}}
        if parent:
            command['parent'] = parent
        self.execute(command)
        ids = [e['id'] for e in json.loads(self.api.noor_doc_scene(self.doc))['entities'] if e['name'] == name and e['parent'] == parent]
        return ids[-1]

    def height(self, terrain, x, z):
        out = ctypes.c_float()
        if self.api.noor_doc_terrain_height(self.doc, terrain.encode(), x, z, ctypes.byref(out)) != 1:
            self.fail('read the ground height')
        return out.value

    def stroke(self, terrain, points, tool, radius, strength, falloff=0.5, seconds=0.25, material=None):
        """One brush stroke through the given world (x, z) points, a dab of `seconds` at each."""
        for i, (x, z) in enumerate(points):
            command = {'terrain': terrain, 'phase': 'begin' if i == 0 else 'dab',
                       'dabs': [{'point': [x, 0, z], 'seconds': seconds}]}
            if i == 0:
                command.update({'tool': tool, 'radius': radius, 'strength': strength, 'falloff': falloff})
                if material:
                    command['material'] = material
            if not self.api.noor_doc_sculpt(self.doc, json.dumps(command).encode()):
                self.fail(f'{tool} the terrain')
        if not self.api.noor_doc_sculpt(self.doc, json.dumps({'terrain': terrain, 'phase': 'end'}).encode()):
            self.fail('end a stroke')


def look_rotation(eye, target):
    """The quaternion turning the camera's forward (-Z) towards `target`: yaw about Y, then pitch."""
    d = [target[i] - eye[i] for i in range(3)]
    yaw = math.atan2(-d[0], -d[2])
    pitch = math.atan2(d[1], math.hypot(d[0], d[2]))
    cy, sy, cp, sp = math.cos(yaw / 2), math.sin(yaw / 2), math.cos(pitch / 2), math.sin(pitch / 2)
    return (cy * sp, sy * cp, -sy * sp, cy * cp)  # q_yaw * q_pitch


def brush(shape, size, material, operation='add', uv_scale=(1, 1), **extra):
    b = {'shape': shape, 'operation': operation, 'size': list(size), 'sides': 16, 'steps': 8,
         'uv_scale': list(uv_scale), 'material': material}
    b.update(extra)
    return {'brush': b}


def main():
    api = load_api(sys.argv[1] if len(sys.argv) > 1 else None)
    # Start clean: the scene and the terrain layers are outputs.
    for rel in (SCENE, 'Assets/Terrain'):
        p = os.path.join(PROJECT, rel)
        if os.path.isdir(p):
            shutil.rmtree(p)
        elif os.path.exists(p):
            os.remove(p)
    # The environment comes from the Milestone 5 sample (CC0, Poly Haven).
    env_dir = os.path.join(PROJECT, 'Assets', 'Environments')
    os.makedirs(env_dir, exist_ok=True)
    shutil.copyfile(os.path.join(ROOT, 'samples', 'PBR-Showroom', 'Assets', 'Environments', 'kloppenheim_06_1k.hdr'),
                    os.path.join(env_dir, 'kloppenheim_06_1k.hdr'))

    doc = Doc(api, PROJECT)
    mats = {}
    for s in ('leafy_grass', 'rock_face', 'dry_ground_01', 'castle_brick_07', 'wooden_rough_planks'):
        if not api.noor_doc_make_material(doc.doc, f'Assets/Textures/{s}/{s}_diff_1k.jpg'.encode()):
            doc.fail(f'make the {s} material')
        mats[s] = doc.guid(f'Assets/Textures/{s}/{s}.noormat')
    # Poly Haven's Leafy Grass is autumn-tan: a greener instance of it for the meadow (what the
    # material editor's instances do: a parent and overrides).
    os.makedirs(os.path.join(PROJECT, 'Assets', 'Materials'), exist_ok=True)
    with open(os.path.join(PROJECT, 'Assets', 'Materials', 'meadow.noormat'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(json.dumps({'noor_material': 1, 'parent': mats['leafy_grass'], 'overrides': {'base_color': [0.55, 0.9, 0.4, 1]}}, indent=2) + '\n')
    mats['meadow'] = doc.guid('Assets/Materials/meadow.noormat')
    if api.noor_doc_new_scene(doc.doc, SCENE.encode(), b'Hillside') != 1:
        doc.fail('create the scene')

    # The land: gentle seeded hills, grass all over (layer 0).
    terrain = doc.create('Hillside', {'terrain': {
        'tiles': [1, 1], 'cell_size': 1, 'base': 'noise', 'seed': 2026, 'height': 10, 'scale': 160, 'octaves': 5,
        'material': mats['meadow'], 'layers': [{'material': mats['meadow'], 'tiling': 0.2}], 'collision': True}})

    house = (20.0, 0.0)  # x, z of the house's centre
    # Sculpt: a level pad for the house, a big hill behind it, a smaller one to the side, smoothed edges.
    pad = [(house[0] + dx, house[1] + dz) for dz in range(-8, 9, 2) for dx in range(-9, 10, 2)]
    doc.stroke(terrain, [house] + pad * 3, 'flatten', radius=6, strength=12, falloff=0.4)
    # Hills as ridges (a line of dabs, several passes), not cones from a single point.
    ridge = [(-60 + 3 * i, 70 - 1.2 * i + 4 * math.sin(i / 3.0)) for i in range(20)]
    # Dabs 3 m apart overlap about 17 times under a 28 m brush: 0.25 s x 1.5 m/s x 17 x 2 passes is
    # about 13 m of ridge, plus a few metres of crest.
    doc.stroke(terrain, ridge * 2, 'raise', radius=28, strength=1.5, falloff=0.95)
    doc.stroke(terrain, ridge[6:14], 'raise', radius=12, strength=1.2, falloff=0.9)  # a crest along the middle
    outcrop = [(6 + 2 * math.cos(a), -6 + 2 * math.sin(a)) for a in range(6)]  # a rocky knoll left of the path
    doc.stroke(terrain, outcrop * 2, 'raise', radius=6, strength=1.5, falloff=0.7)
    knoll = [(70 + 2 * i, 35 + i) for i in range(8)]
    doc.stroke(terrain, knoll * 2, 'raise', radius=16, strength=4, falloff=0.9)
    rim = [(house[0] + 11 * math.cos(a / 12 * math.tau), house[1] + 10 * math.sin(a / 12 * math.tau)) for a in range(13)]
    doc.stroke(terrain, rim * 2, 'smooth', radius=5, strength=1.5)
    # Paint: rock on the hilltops, a dirt path from the south up to the door, a worn patch by the house.
    doc.stroke(terrain, ridge[5:15] * 2, 'paint', radius=9, strength=1.5, falloff=0.8, material=mats['rock_face'])
    doc.stroke(terrain, knoll[2:6], 'paint', radius=8, strength=1.5, falloff=0.8, material=mats['rock_face'])
    doc.stroke(terrain, outcrop * 2, 'paint', radius=5, strength=2, falloff=0.6, material=mats['rock_face'])
    path = [(house[0] + 1.5 * math.sin(z / 9.0), z) for z in range(-60, -2, 2)]
    doc.stroke(terrain, path, 'paint', radius=2.4, strength=4, falloff=0.5, material=mats['dry_ground_01'])
    doc.stroke(terrain, [(house[0] + dx, house[1] - 5) for dx in (-3, 0, 3)], 'paint', radius=4, strength=3, falloff=0.7, material=mats['dry_ground_01'])
    # Each layer's tiling: the textures are about 2 m (grass, dirt) and 4 m (rock) across.
    t = next(e for e in json.loads(api.noor_doc_scene(doc.doc))['entities'] if e['id'] == terrain)['components']['terrain']
    for layer in t['layers']:
        layer['tiling'] = 0.125 if layer['material'] == mats['rock_face'] else 0.2 if layer['material'] == mats['meadow'] else 0.5
    doc.execute({'op': 'set_component', 'entity': terrain, 'type': 'terrain', 'value': t})

    # The house: brick walls from brushes, a doorway and windows cut out, a plank floor and roof.
    floor_y = doc.height(terrain, *house)
    model = doc.create('House', {'csg_model': {}, 'collider': {'shape': 'mesh'}}, (house[0], floor_y, house[1]))
    brick, planks = mats['castle_brick_07'], mats['wooden_rough_planks']
    W, D, H, T = 8.0, 6.0, 3.0, 0.3  # outside width (x), depth (z), wall height, wall thickness
    walls = [('South wall', (W, H, T), (0, H / 2, -(D - T) / 2)), ('North wall', (W, H, T), (0, H / 2, (D - T) / 2)),
             ('West wall', (T, H, D - 2 * T), (-(W - T) / 2, H / 2, 0)), ('East wall', (T, H, D - 2 * T), ((W - T) / 2, H / 2, 0))]
    for name, size, pos in walls:
        doc.create(name, brush('box', size, brick, uv_scale=(0.5, 0.5)), pos, parent=model)
    doc.create('Floor', brush('box', (W - 2 * T, 0.1, D - 2 * T), planks), (0, 0.05, 0), parent=model)
    doc.create('Doorway', brush('arch', (1.2, 2.3, 1.0), brick, operation='subtract'), (0, 1.15, -(D - T) / 2), parent=model)
    for x in (-(W - T) / 2, (W - T) / 2):
        doc.create('Window', brush('box', (1.0, 1.0, 1.2), brick, operation='subtract'), (x, 1.7, 0), parent=model,
                   rotation=(0, math.sin(math.pi / 4), 0, math.cos(math.pi / 4)))
    doc.create('Roof', brush('box', (W + 0.6, 0.3, D + 0.6), planks), (0, H + 0.15, 0), parent=model)
    doc.create('Chimney', brush('box', (0.8, 1.4, 0.8), brick), (2.4, H + 1.0, 1.4), parent=model)

    # Light, sky, a camera on the path, and a walker that follows the path into the house.
    doc.create('Sun', {'light': {'type': 'directional', 'color': '#FFF1DE', 'illuminance': 100000}}, (0, 30, 0),
               rotation=(-0.33, 0.30, 0.11, 0.89))
    doc.create('Environment', {'environment': {'hdri': doc.guid('Assets/Environments/kloppenheim_06_1k.hdr'),
                                               'intensity': 8000, 'rotation': 120, 'background': True}})
    eye = (house[0] + 13, floor_y + 4.5, house[1] - 24)
    doc.create('Camera', {'camera': {'fov': 60, 'near': 0.1, 'ev100': 14.5}}, eye,
               rotation=look_rotation(eye, (house[0] - 6, floor_y + 3, house[1] + 8)))
    start = (house[0], -40.0)
    doc.create('Walker', {
        'character': {'radius': 0.3, 'height': 1.8, 'step_height': 0.35, 'max_slope': 50},
        'script': {'file': 'Scripts/Walker.luau', 'params': {'speed': 3, 'x': 0, 'z': 1}},
        'primitive': {'shape': 'capsule', 'radius': 0.3, 'height': 1.8, 'color': '#F2C14E'}},
        (start[0], doc.height(terrain, *start) + 0.1, start[1]))

    if api.noor_doc_save_scene(doc.doc) != 1:
        doc.fail('save')
    api.noor_doc_close(doc.doc)
    print(f'Built {SCENE}: house floor at {floor_y:.2f} m; walker starts at z = {start[1]}')


if __name__ == '__main__':
    main()
