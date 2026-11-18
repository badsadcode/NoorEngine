"""Grows a forest on samples/Hillside (Milestone 7, part 8), through the editor API like the rest of the
sample (tools/samples/build_hillside.py builds the land and the house first).

    python tools/samples/add_hillside_forest.py [path/to/noor_editor_api.dll]

Inputs: the three NoorMD oaks in samples/Hillside/Assets/Trees (Oak 01, 04 and 07 from a NoorForm
variety pack, converted to the NoorEngine export layout by tools/interchange/noormd_engine_export.py).
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
    nodes.append({'id': 'all', 'type': 'merge', 'params': {'spacing': 9}, 'position': [1340, 240]})
    links += [{'from': 'oak_01.instances', 'to': 'mix.a'}, {'from': 'oak_04.instances', 'to': 'mix.b'},
              {'from': 'mix.instances', 'to': 'all.a'}, {'from': 'oak_07.instances', 'to': 'all.b'}]
    return {'noor_graph': 1, 'seed': 2026, 'nodes': nodes, 'links': links, 'outputs': [{'name': 'trees', 'from': 'all.instances'}]}


def main():
    api = load_api(sys.argv[1] if len(sys.argv) > 1 else None)
    api.noor_doc_open_scene.restype, api.noor_doc_open_scene.argtypes = ctypes.c_int32, [ctypes.c_void_p, ctypes.c_char_p]
    if api.noor_editor_api_version() < 23:
        sys.exit('this script needs editor API v23 or newer (the paint_mask and scatter nodes)')
    for name in OAKS:
        if not os.path.exists(os.path.join(PROJECT, 'Assets', 'Trees', f'{name}.glb')):
            sys.exit(f'Assets/Trees/{name}.glb is missing (see this script\'s header)')
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
    old = [e['id'] for e in scene['entities'] if e['name'] == 'Forest']
    for eid in old:  # running it again replaces the forest (the hand-made edits too)
        doc.execute({'op': 'remove', 'entity': eid})
    doc.create('Forest', {'scatter': {'graph': doc.guid('Assets/Graphs/forest.noorgraph'), 'output': 'trees', 'ground': terrain,
                                      'removed': [], 'added': []}})
    if api.noor_doc_save_scene(doc.doc) != 1:
        doc.fail('save')
    api.noor_doc_close(doc.doc)
    print(f'Grew the forest in {SCENE} (Assets/Graphs/forest.noorgraph)')


if __name__ == '__main__':
    main()
