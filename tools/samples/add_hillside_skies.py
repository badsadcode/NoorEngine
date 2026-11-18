"""Hillside under other skies (Milestone 8A): scenes of the Hillside sample with the same land, house and forest
under the physically based sky, each a golden render:
  - Hillside_Haze (part 3): a late summer afternoon with valley fog and aerial perspective, the far hills fading
    into the sky without a seam (tests/golden/Hillside_Haze.png);
  - Hillside_Night (part 5): the same valley at 22:00 under a full moon, automatic exposure, the house and the
    trees against the moonlit sky and the stars (tests/golden/Hillside_Night.png).

    python tools/samples/add_hillside_skies.py

Reads samples/Hillside/Scenes/Hillside.noorscene (built by build_hillside.py and add_hillside_forest.py) and
writes the scenes next to it. Run it again after rebuilding Hillside.
"""
import json
import pathlib
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCENES = ROOT / 'samples' / 'Hillside' / 'Scenes'

# Hillside_Haze: a late summer afternoon, a low valley haze (thick below 3 m, halving every 7 m above), and air that
# makes the 256 m world read like a few kilometres.
HAZE = {
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

# Hillside_Night: 22:00 at the end of August, a full moon about 22 degrees up in front of the camera ("north" turns
# the compass so it is), a little valley mist, the camera metering the light itself.
NIGHT = {
    'sky': 'atmosphere',
    'turbidity': 1.5,
    'ground_albedo': 0.25,
    'time_of_day': 22.0,
    'latitude': 50,
    'day_of_year': 230,
    'north': -74,
    'moon_phase': 0.5,
    'stars': True,
    'fog_density': 0.003,
    'fog_height': 2,
    'fog_falloff': 0.15,
    'fog_anisotropy': 0.5,
    'fog_color': '#FFFFFF',
    'aerial_scale': 30,
    'background': True,
}


def write(name, environment, ev100):
    scene = json.loads((SCENES / 'Hillside.noorscene').read_text(encoding='utf-8'))
    # The same entities (same ids): the cooked house and the forest's placements are keyed by entity id and
    # are shared with Hillside.noorscene. Only the scene itself gets its own id (derived, so stable).
    scene['id'] = str(uuid.uuid5(uuid.UUID('9a2e6b1c-7c3f-4d4e-9b8a-2f5e1d0c4a77'), scene['id'] + ('' if name == 'Hillside_Haze' else name)))
    scene['name'] = name
    for e in scene['entities']:
        components = e['components']
        if 'environment' in components:
            components['environment'] = dict(environment)
        if 'light' in components and components['light'].get('type') == 'directional':
            components['light'] = {'type': 'directional', 'illuminance': 100000}  # the sky colours it
        if 'camera' in components:
            components['camera']['ev100'] = ev100
    (SCENES / f'{name}.noorscene').write_text(json.dumps(scene, indent=2) + '\n', encoding='utf-8', newline='\n')
    print('wrote', SCENES / f'{name}.noorscene')


def main():
    write('Hillside_Haze', HAZE, 14.0)
    write('Hillside_Night', NIGHT, 'auto')


if __name__ == '__main__':
    main()
