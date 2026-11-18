"""Hillside in haze (Milestone 8A, part 3): a second scene of the Hillside sample, the same land, house and
forest under the physically based sky, with height fog in the valley and aerial perspective, so the far hills
fade into the sky without a seam. It's the fog's golden render (tests/golden/Hillside_Haze.png).

    python tools/samples/add_hillside_haze.py

Reads samples/Hillside/Scenes/Hillside.noorscene (built by build_hillside.py and add_hillside_forest.py) and
writes samples/Hillside/Scenes/Hillside_Haze.noorscene next to it. Run it again after rebuilding Hillside.
"""
import json
import pathlib
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCENES = ROOT / 'samples' / 'Hillside' / 'Scenes'

# The look: a late summer afternoon, a low valley haze (thick below 3 m, halving every 7 m above), and air that
# makes the 256 m world read like a few kilometres.
ENVIRONMENT = {
    'sky': 'atmosphere',
    'turbidity': 2,
    'ground_albedo': 0.25,
    'time_of_day': 17.0,
    'latitude': 50,
    'day_of_year': 230,
    'north': 0,
    'fog_density': 0.006,
    'fog_height': 3,
    'fog_falloff': 0.1,
    'fog_anisotropy': 0.6,
    'fog_color': '#FFFFFF',
    'aerial_scale': 30,
    'background': True,
}


def main():
    scene = json.loads((SCENES / 'Hillside.noorscene').read_text(encoding='utf-8'))
    # The same entities (same ids): the cooked house and the forest's placements are keyed by entity id and
    # are shared with Hillside.noorscene. Only the scene itself gets its own id (derived, so stable).
    scene['id'] = str(uuid.uuid5(uuid.UUID('9a2e6b1c-7c3f-4d4e-9b8a-2f5e1d0c4a77'), scene['id']))
    scene['name'] = 'Hillside_Haze'
    for e in scene['entities']:
        components = e['components']
        if 'environment' in components:
            components['environment'] = dict(ENVIRONMENT)
        if 'light' in components and components['light'].get('type') == 'directional':
            components['light'] = {'type': 'directional', 'illuminance': 100000}  # the sky colours it
        if 'camera' in components:
            components['camera']['ev100'] = 14.0
    (SCENES / 'Hillside_Haze.noorscene').write_text(json.dumps(scene, indent=2) + '\n', encoding='utf-8', newline='\n')
    print('wrote', SCENES / 'Hillside_Haze.noorscene')


if __name__ == '__main__':
    main()
