"""What the 8D sample costs (Milestone 8D, part 8: the 8D review's performance notes).

    python tools/samples/measure_hillside_water.py

Draws samples/Hillside/Scenes/Hillside_Water.noorscene (tools/samples/add_hillside_water.py: the lake in the rain, a
stream, four floating crates, Alden swimming) for 400 frames with noor_player, against variants: without its water at
all, without the stream, without the rain, and with 100 floating crates (the CPU cost of floating). Prints each one's
fastest GPU frame (the best of three runs), its Scene and See-through passes, and its game update (scripts, physics with
floating, the animator: the median frame). Works on a temporary copy of the sample, so nothing is written to it. Needs a
build.
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


def variants(sample):
    def without(*names):
        s = copy.deepcopy(sample)
        s['entities'] = [e for e in s['entities'] if e['name'] not in names]
        return s

    def dry():
        s = copy.deepcopy(sample)
        s['entities'] = [e for e in s['entities'] if 'water' not in e['components']]
        return s

    def no_rain():
        s = copy.deepcopy(sample)
        for e in s['entities']:
            if 'environment' in e['components']:
                e['components']['environment']['rain'] = 0
        return s

    def crates(n):
        s = copy.deepcopy(sample)
        crate = next(e for e in s['entities'] if e['name'] == 'Crate 1')
        for k in range(n - 4):
            c = copy.deepcopy(crate)
            c['id'] = str(uuid.uuid5(uuid.NAMESPACE_OID, f'measure crate {k}'))
            c['name'] = f'Crate x{k}'
            a = 2 * math.pi * k / (n - 4)
            r = 1.0 + 4.0 * (k % 5) / 4
            c['transform']['position'] = [61.0 + r * math.cos(a), 3.7 + 0.6 * (k % 3), 68.0 + r * math.sin(a)]
            s['entities'].append(c)
        return s

    return {'Hillside, no water': dry(), 'the sample': sample, 'without the stream': without('Stream'),
            'without the rain': no_rain(), '100 floating crates': crates(100)}


def main():
    player = find('noor_player.exe', ROOT / 'build' / 'ninja' / 'tools' / 'noor_player')
    with tempfile.TemporaryDirectory() as tmp:
        project = pathlib.Path(tmp) / 'Hillside'
        shutil.copytree(SAMPLE, project)  # with .noor: the cooked forest
        sample = json.loads((project / 'Scenes' / 'Hillside_Water.noorscene').read_text(encoding='utf-8'))
        print(f"{'scene':<22} {'GPU (fastest)':>14} {'Scene':>8} {'See-through':>12} {'game update':>12}")
        for name, scene in variants(sample).items():
            path = project / 'Scenes' / '_measure.noorscene'
            path.write_text(json.dumps(scene), encoding='utf-8')
            best = None
            for _ in range(RUNS):
                report = pathlib.Path(tmp) / 'report.json'
                r = subprocess.run([str(player), str(project), '--scene', 'Scenes/_measure.noorscene', '--frames', '400', '--no-audio',
                                    '--no-input', '--report', str(report), '--log', str(pathlib.Path(tmp) / 'player.log')], capture_output=True)
                if r.returncode != 0 or not report.exists():
                    break
                t = json.loads(report.read_text(encoding='utf-8'))['timing_ms']
                row = (t['gpu']['min'], t['scene_gpu']['min'], t['see_through_gpu']['min'], t['game']['median'])
                best = row if best is None or row[0] < best[0] else best
                report.unlink()
            if best is None:
                print(f'{name:<22} FAILED ({r.returncode}); see the log in {tmp}')
                continue
            print(f'{name:<22} {best[0]:>11.2f} ms {best[1]:>5.2f} ms {best[2]:>9.3f} ms {best[3]:>9.2f} ms')


if __name__ == '__main__':
    main()
