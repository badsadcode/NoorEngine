"""What the 8B sample costs, ingredient by ingredient (Milestone 8B, part 8: the 8B review's performance notes).

    python tools/samples/measure_hillside_storm.py

Draws samples/Hillside/Scenes/Hillside_Storm.noorscene (tools/samples/add_hillside_storm.py) for 400 frames with
noor_player, then variants of it with one ingredient taken out: no rain, no campfire, no Coth, no storm at all (the
calm Dusk it starts from). And the particle-heavy benchmark: the storm with twenty campfires (the fire, smoke and
sparks copied round the valley, the sparks still landing on the hillside). Prints each one's fastest GPU frame (the work
itself: a desktop GPU idles clocked down and is shared; the best of three runs), its particle simulation (CPU) and how many particles it had.
Works on a temporary copy of the sample, so nothing is written to it. Needs a build (cmake --build --preset ninja-release).
"""
import copy
import json
import math
import pathlib
import shutil
import subprocess
import sys
import tempfile
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[2]
SAMPLE = ROOT / 'samples' / 'Hillside'
RUNS = 3


def find(name, under):
    found = sorted(under.rglob(name), key=lambda p: p.stat().st_mtime, reverse=True)
    if not found:
        sys.exit(f'{name} not found under {under}: build first')
    return found[0]


def variants(storm):
    def without(pred):
        s = copy.deepcopy(storm)
        s['entities'] = [e for e in s['entities'] if not pred(e)]
        return s

    def no_rain():
        s = copy.deepcopy(storm)
        for e in s['entities']:
            if 'weather_zone' in e['components']:
                e['components']['weather_zone']['environment']['rain'] = 0
        return s

    def campfires(n):
        s = copy.deepcopy(storm)
        fire = [e for e in s['entities'] if 'emitter' in e['components'] or e['name'] == 'Firelight']
        for k in range(1, n):
            a = 2 * math.pi * k / n
            dx, dz = 14 * math.cos(a), 14 * math.sin(a)  # a ring round the first, 14 m out
            for e in fire:
                c = copy.deepcopy(e)
                c['id'] = str(uuid.uuid5(uuid.NAMESPACE_OID, f'{e["id"]}-{k}'))
                c['name'] = f'{e["name"]} {k}'
                p = c['transform']['position']
                c['transform']['position'] = [p[0] + dx, p[1] + 1.0, p[2] + dz]  # a metre up: the ground isn't flat
                s['entities'].append(c)
        return s

    def settled():  # the zone fully on from the start, no script touching it
        s = copy.deepcopy(storm)
        for e in s['entities']:
            if 'weather_zone' in e['components']:
                e['components']['weather_zone']['weight'] = 1
                e['components'].pop('script', None)
        return s

    return {
        'Storm (the sample)': storm,
        'storm, no roll-in script': settled(),
        'no rain': no_rain(),
        'no campfire': without(lambda e: 'emitter' in e['components'] or e['name'] == 'Firelight'),
        'no Coth': without(lambda e: e['name'] in ('Coth', 'Coth sprite')),
        'no storm (calm Dusk)': without(lambda e: 'weather_zone' in e['components']),
        'twenty campfires': campfires(20),
    }


def main():
    player = find('noor_player.exe', ROOT / 'build' / 'ninja' / 'tools' / 'noor_player')
    with tempfile.TemporaryDirectory() as tmp:
        project = pathlib.Path(tmp) / 'Hillside'
        shutil.copytree(SAMPLE, project)  # with .noor: the cooked forest
        storm = json.loads((project / 'Scenes' / 'Hillside_Storm.noorscene').read_text(encoding='utf-8'))
        print(f"{'scene':<24} {'GPU (fastest)':>14} {'particles CPU':>14} {'particles':>10}")
        for name, scene in variants(storm).items():
            path = project / 'Scenes' / f'_measure.noorscene'
            path.write_text(json.dumps(scene), encoding='utf-8')
            best = None
            for run in range(RUNS):  # the best of a few runs: on the whole sample, one run's fastest frame still swings
                report = pathlib.Path(tmp) / 'report.json'
                r = subprocess.run([str(player), str(project), '--scene', 'Scenes/_measure.noorscene', '--frames', '400', '--no-audio',
                                    '--no-input', '--report', str(report), '--log', str(pathlib.Path(tmp) / 'player.log')], capture_output=True)
                if r.returncode != 0 or not report.exists():
                    break
                data = json.loads(report.read_text(encoding='utf-8'))
                gpu = data['timing_ms']['gpu']['min']
                sim = (data['timing_ms'].get('particles') or {}).get('median', 0)
                alive = data.get('particles', {}).get('alive', 0)
                best = (gpu, sim, alive) if best is None or gpu < best[0] else best
                report.unlink()
            if best is None:
                print(f'{name:<24} FAILED ({r.returncode})')
                continue
            print(f'{name:<24} {best[0]:>11.2f} ms {best[1]:>11.3f} ms {best[2]:>10}')


if __name__ == '__main__':
    main()
