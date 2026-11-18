"""Grows a forest on samples/Hillside (Milestone 7, part 8), through the editor API like the rest of the
sample (tools/samples/build_hillside.py builds the land and the house first).

    python tools/samples/add_hillside_forest.py [path/to/noor_editor_api.dll]

Inputs: three NoorMD oaks in samples/Hillside/Assets/Trees (Oak 01, 04 and 07) and three rocks in
Assets/Rocks (Cracked rock 01, 04 and 07), from NoorForm packs, converted to the NoorEngine export
layout by tools/interchange/noormd_engine_export.py.
It writes the forest's graph (Assets/Graphs/forest.noorgraph: what any graph editor user would save),
then adds a `Forest` scatter entity standing on the terrain and saves; saving cooks the graph.

The graph plants the oaks only where the meadow was painted (not on the dirt path or the rock), keeps
a corridor along the path, a circle around the house and the camera's view of the house clear, and
mixes the three oaks (each its own scatter, merged so they keep apart).
"""
import ctypes
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_hillside import PROJECT, SCENE, Doc, load_api  # noqa: E402

OAKS = ['Oak_01', 'Oak_04', 'Oak_07']
ROCKS = ['Cracked_rock_01', 'Cracked_rock_04', 'Cracked_rock_07']
GRASSES = ['Short_grass', 'Tall_grass']


def graph(doc):
    paint = doc.guid('Assets/Terrain/Hillside.noorpaint')
    oak = {name: doc.guid(f'Assets/Trees/{name}.glb') for name in OAKS}
    nodes = [
        {'id': 'meadow', 'type': 'paint_mask', 'params': {'tiles_x': 1, 'tiles_z': 1, 'cell_size': 1, 'paint': paint, 'layer': 0}, 'position': [40, 40]},
        # Mostly meadow only: the edges of the path and the rock stay clear too.
        {'id': 'grassy', 'type': 'remap', 'params': {'from_min': 0.6, 'from_max': 0.95, 'to_min': 0, 'to_max': 1, 'unit': 'ratio'}, 'position': [300, 40]},
        # The path (a 10 m corridor), the house and its yard, the camera's view of the house.
        {'id': 'clear', 'type': 'exclude', 'params': {'tiles_x': 1, 'tiles_z': 1, 'cell_size': 1,
                                                       'paths': '20,-75 20,-4; 33,-24 14,8', 'width': 10, 'circles': '20,0,13', 'soft': 4},
         'position': [40, 260]},
        {'id': 'where', 'type': 'combine', 'params': {'op': 'multiply'}, 'position': [560, 140]},
    ]
    links = [{'from': 'meadow.mask', 'to': 'grassy.in'}, {'from': 'grassy.out', 'to': 'where.a'}, {'from': 'clear.mask', 'to': 'where.b'}]
    for i, name in enumerate(OAKS):
        node = name.lower()
        nodes.append({'id': node, 'type': 'scatter', 'params': {'asset': oak[name], 'density': 0.12, 'spacing': 12, 'scale_min': 0.8,
                                                                 'scale_max': 1.15, 'width': 256, 'depth': 256}, 'position': [820, 40 + 220 * i]})
        links.append({'from': 'where.out', 'to': f'{node}.mask'})
    nodes.append({'id': 'mix', 'type': 'merge', 'params': {'spacing': 9}, 'position': [1080, 140]})
    nodes.append({'id': 'trees', 'type': 'merge', 'params': {'spacing': 9}, 'position': [1340, 240]})
    links += [{'from': 'oak_01.instances', 'to': 'mix.a'}, {'from': 'oak_04.instances', 'to': 'mix.b'},
              {'from': 'mix.instances', 'to': 'trees.a'}, {'from': 'oak_07.instances', 'to': 'trees.b'}]
    # Rocks: where the rock was painted (layer 1: the ridge, the knoll, the outcrop), a few in the meadow,
    # never on the path or in the yard; trees win where they meet.
    rock = {name: doc.guid(f'Assets/Rocks/{name}.glb') for name in ROCKS}
    nodes += [
        {'id': 'rocky', 'type': 'paint_mask', 'params': {'tiles_x': 1, 'tiles_z': 1, 'cell_size': 1, 'paint': paint, 'layer': 1}, 'position': [40, 700]},
        {'id': 'stony', 'type': 'remap', 'params': {'from_min': 0.25, 'from_max': 0.7, 'to_min': 0, 'to_max': 1, 'unit': 'ratio'}, 'position': [300, 700]},
        {'id': 'outcrops', 'type': 'combine', 'params': {'op': 'multiply'}, 'position': [560, 700]},
    ]
    links += [{'from': 'rocky.mask', 'to': 'stony.in'}, {'from': 'stony.out', 'to': 'outcrops.a'}, {'from': 'clear.mask', 'to': 'outcrops.b'}]
    for i, name in enumerate(ROCKS):
        node = name.lower().replace('cracked_', '')
        nodes.append({'id': node, 'type': 'scatter', 'params': {'asset': rock[name], 'density': 0.9, 'spacing': 5, 'scale_min': 0.5,
                                                                 'scale_max': 1.3, 'width': 256, 'depth': 256}, 'position': [820, 700 + 220 * i]})
        links.append({'from': 'outcrops.out', 'to': f'{node}.mask'})
    nodes.append({'id': 'meadow_rocks', 'type': 'scatter', 'params': {'asset': rock['Cracked_rock_04'], 'density': 0.02, 'spacing': 20,
                                                                       'scale_min': 0.3, 'scale_max': 0.7, 'width': 256, 'depth': 256},
                  'position': [820, 1360]})
    links.append({'from': 'where.out', 'to': 'meadow_rocks.mask'})
    nodes += [{'id': 'rocks_a', 'type': 'merge', 'params': {'spacing': 3}, 'position': [1080, 760]},
              {'id': 'rocks_b', 'type': 'merge', 'params': {'spacing': 3}, 'position': [1340, 860]},
              {'id': 'rocks', 'type': 'merge', 'params': {'spacing': 3}, 'position': [1600, 960]},
              {'id': 'forest', 'type': 'merge', 'params': {'spacing': 4}, 'position': [1860, 480]}]
    links += [{'from': 'rock_01.instances', 'to': 'rocks_a.a'}, {'from': 'rock_04.instances', 'to': 'rocks_a.b'},
              {'from': 'rocks_a.instances', 'to': 'rocks_b.a'}, {'from': 'rock_07.instances', 'to': 'rocks_b.b'},
              {'from': 'rocks_b.instances', 'to': 'rocks.a'}, {'from': 'meadow_rocks.instances', 'to': 'rocks.b'},
              {'from': 'trees.instances', 'to': 'forest.a'}, {'from': 'rocks.instances', 'to': 'forest.b'}]
    # Grass: thick on the meadow (not on the path or the rock) around the house and the path, where the camera
    # and the walker are, short with tufts of tall. Its own output, for a scatter with a draw distance and no
    # shadows (thousands of small clumps).
    grass = {name: doc.guid(f'Assets/Plants/{name}.glb') for name in GRASSES}
    nodes += [
        {'id': 'near', 'type': 'exclude', 'params': {'tiles_x': 1, 'tiles_z': 1, 'cell_size': 1, 'circles': '22,-16,55', 'soft': 12}, 'position': [40, 1600]},
        {'id': 'grass_area', 'type': 'remap', 'params': {'from_min': 0, 'from_max': 1, 'to_min': 1, 'to_max': 0, 'unit': 'ratio'}, 'position': [300, 1600]},
        {'id': 'lawn', 'type': 'combine', 'params': {'op': 'multiply'}, 'position': [560, 1600]},
        {'id': 'short_grass', 'type': 'scatter', 'params': {'asset': grass['Short_grass'], 'density': 300, 'spacing': 0.45, 'scale_min': 1.2,
                                                             'scale_max': 2.0, 'width': 256, 'depth': 256, 'max_count': 200000},
         'position': [820, 1600]},
        {'id': 'tall_grass', 'type': 'scatter', 'params': {'asset': grass['Tall_grass'], 'density': 15, 'spacing': 1.2, 'scale_min': 0.8,
                                                            'scale_max': 1.3, 'width': 256, 'depth': 256, 'max_count': 200000},
         'position': [820, 1820]},
        {'id': 'grass', 'type': 'merge', 'params': {'spacing': 0.35}, 'position': [1080, 1700]},
    ]
    links += [{'from': 'near.mask', 'to': 'grass_area.in'}, {'from': 'grass_area.out', 'to': 'lawn.a'}, {'from': 'grassy.out', 'to': 'lawn.b'},
              {'from': 'lawn.out', 'to': 'short_grass.mask'}, {'from': 'lawn.out', 'to': 'tall_grass.mask'},
              {'from': 'tall_grass.instances', 'to': 'grass.a'}, {'from': 'short_grass.instances', 'to': 'grass.b'}]
    return {'noor_graph': 1, 'seed': 2026, 'nodes': nodes, 'links': links,
            'outputs': [{'name': 'forest', 'from': 'forest.instances'}, {'name': 'grass', 'from': 'grass.instances'}]}


def main():
    api = load_api(sys.argv[1] if len(sys.argv) > 1 else None)
    api.noor_doc_open_scene.restype, api.noor_doc_open_scene.argtypes = ctypes.c_int32, [ctypes.c_void_p, ctypes.c_char_p]
    if api.noor_editor_api_version() < 23:
        sys.exit('this script needs editor API v23 or newer (the paint_mask and scatter nodes)')
    for folder, names in (('Trees', OAKS), ('Rocks', ROCKS), ('Plants', GRASSES)):
        for name in names:
            if not os.path.exists(os.path.join(PROJECT, 'Assets', folder, f'{name}.glb')):
                sys.exit(f'Assets/{folder}/{name}.glb is missing (see this script\'s header)')
    doc = Doc(api, PROJECT)
    doc.assets()  # scan: the oaks get their ids from their manifests
    os.makedirs(os.path.join(PROJECT, 'Assets', 'Graphs'), exist_ok=True)
    with open(os.path.join(PROJECT, 'Assets', 'Graphs', 'forest.noorgraph'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(json.dumps(graph(doc), indent=2) + '\n')
    doc.assets()
    if api.noor_doc_open_scene(doc.doc, SCENE.encode()) != 1:
        doc.fail('open the scene')
    scene = json.loads(api.noor_doc_scene(doc.doc))
    terrain = next(e['id'] for e in scene['entities'] if e['name'] == 'Hillside')
    old = [e['id'] for e in scene['entities'] if e['name'] in ('Forest', 'Grass')]
    for eid in old:  # running it again replaces the forest (the hand-made edits too)
        doc.execute({'op': 'remove', 'entity': eid})
    doc.create('Forest', {'scatter': {'graph': doc.guid('Assets/Graphs/forest.noorgraph'), 'output': 'forest', 'ground': terrain,
                                      'removed': [], 'added': []}})
    doc.create('Grass', {'scatter': {'graph': doc.guid('Assets/Graphs/forest.noorgraph'), 'output': 'grass', 'ground': terrain,
                                     'draw_distance': 45, 'shadows': False, 'removed': [], 'added': []}})
    if api.noor_doc_save_scene(doc.doc) != 1:
        doc.fail('save')
    api.noor_doc_close(doc.doc)
    print(f'Grew the forest in {SCENE} (Assets/Graphs/forest.noorgraph)')


if __name__ == '__main__':
    main()
