"""Hillside in rain and in snow (Milestone 8B, part 3: weather particles).

    python tools/samples/add_hillside_weather.py

Writes samples/Hillside/Scenes/Weather/Hillside_Rain.noorscene and Hillside_Snow.noorscene from the Overcast preset
scene (Scenes/Presets/Hillside_Overcast.noorscene, written by add_hillside_presets.py): the environment still points
at the project's Overcast environment, with the weather written beside it as overrides, as an author would in the
Inspector. Rain: heavy, a 5 m/s breeze. Snow: a January morning, steady snow, a light breeze, a little fog.
Golden renders both. Run add_hillside_presets.py first.
"""
import json
import pathlib
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCENES = ROOT / 'samples' / 'Hillside' / 'Scenes'
NAMESPACE = uuid.UUID('3c9e2a71-5d4b-4f08-b6e1-7a2d9c4e1f30')

WEATHER = {
    'Hillside_Rain': {'rain': 1.0, 'wind_speed': 5, 'wind_direction': 60},
    'Hillside_Snow': {'snow': 1.0, 'wind_speed': 1.5, 'wind_direction': 60, 'time_of_day': 10.5, 'day_of_year': 20,
                      'fog_density': 0.004, 'fog_color': '#E8ECF2'},
}


def main():
    base = json.loads((SCENES / 'Presets' / 'Hillside_Overcast.noorscene').read_text(encoding='utf-8'))
    out = SCENES / 'Weather'
    out.mkdir(exist_ok=True)
    for name, overrides in WEATHER.items():
        scene = json.loads(json.dumps(base))
        scene['name'] = name
        scene['id'] = str(uuid.uuid5(NAMESPACE, name))
        for e in scene['entities']:
            env = e['components'].get('environment')
            if env is not None:
                env.update(overrides)
        (out / f'{name}.noorscene').write_text(json.dumps(scene, indent=2) + '\n', encoding='utf-8')
        print('wrote', f'Scenes/Weather/{name}.noorscene')


if __name__ == '__main__':
    main()
