"""What the 8C sample costs, and the crowd benchmark (Milestone 8C, part 8: the 8C review's performance notes).

    python tools/samples/measure_hillside_alden.py

Draws samples/Hillside/Scenes/Hillside_Alden.noorscene (tools/samples/add_hillside_alden.py) for 400 frames with
noor_player, against the valley without Alden, and two crowds: 100 and 250 Aldens, each a full copy (his own character
controller, wandering script, controller blending idle, walk and run, root motion and staff), their loops spread round
the valley. Prints each one's fastest GPU frame (the best of three runs), its game update (scripts, physics, the
animator: the median frame) and gathering the scene for drawing (median), and how many characters were posed.
Works on a temporary copy of the sample, so nothing is written to it. Needs a build.
"""
import copy
import json
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


def subtree(entities, root_id):
    ids, changed = {root_id}, True
    while changed:
        changed = False
        for e in entities:
            if e.get('parent') in ids and e['id'] not in ids:
                ids.add(e['id'])
                changed = True
    return [e for e in entities if e['id'] in ids]


def variants(sample):
    walker = next(e for e in sample['entities'] if e['name'] == 'Alden Wayfarer')
    group = subtree(sample['entities'], walker['id'])

    def without_alden():
        s = copy.deepcopy(sample)
        gone = {e['id'] for e in group}
        s['entities'] = [e for e in s['entities'] if e['id'] not in gone]
        return s

    def crowd(n):
        s = copy.deepcopy(sample)
        text = json.dumps(group)
        side = int(n ** 0.5 + 0.999)
        for k in range(1, n):
            dx, dz = (k % side - side // 2) * 5.0, (k // side - side // 2) * 5.0  # loops 5 m apart round the first
            t = text
            for e in group:
                t = t.replace(e['id'], str(uuid.uuid5(uuid.NAMESPACE_OID, f'{e["id"]}-{k}')))
            copies = json.loads(t)
            for c in copies:
                if c['id'] == str(uuid.uuid5(uuid.NAMESPACE_OID, f'{walker["id"]}-{k}')):
                    p = c['transform']['position']
                    c['transform']['position'] = [p[0] + dx, p[1] + 6.0, p[2] + dz]  # from above: they land on the slope
                    params = c['components']['script']['params']
                    params['points'] = [[x + dx, z + dz] for x, z in params['points']]
            s['entities'] += copies
        return s

    return {'Fair Weather, no Alden': without_alden(), 'the sample (Alden)': sample, '100 Aldens': crowd(100), '250 Aldens': crowd(250)}


def main():
    player = find('noor_player.exe', ROOT / 'build' / 'ninja' / 'tools' / 'noor_player')
    with tempfile.TemporaryDirectory() as tmp:
        project = pathlib.Path(tmp) / 'Hillside'
        shutil.copytree(SAMPLE, project)  # with .noor: the cooked forest
        sample = json.loads((project / 'Scenes' / 'Hillside_Alden.noorscene').read_text(encoding='utf-8'))
        print(f"{'scene':<24} {'GPU (fastest)':>14} {'game update':>12} {'gathering':>10} {'posed':>6}")
        for name, scene in variants(sample).items():
            path = project / 'Scenes' / '_measure.noorscene'
            path.write_text(json.dumps(scene), encoding='utf-8')
            best = None
            for run in range(RUNS):
                report = pathlib.Path(tmp) / 'report.json'
                r = subprocess.run([str(player), str(project), '--scene', 'Scenes/_measure.noorscene', '--frames', '400', '--no-audio',
                                    '--no-input', '--report', str(report), '--log', str(pathlib.Path(tmp) / 'player.log')], capture_output=True)
                if r.returncode != 0 or not report.exists():
                    break
                data = json.loads(report.read_text(encoding='utf-8'))
                t = data['timing_ms']
                row = (t['gpu']['min'], t['game']['median'], t['collect']['median'], data.get('animation', {}).get('posed', 0))
                best = row if best is None or row[0] < best[0] else best
                report.unlink()
            if best is None:
                print(f'{name:<24} FAILED ({r.returncode}); see the log in {tmp}')
                continue
            print(f'{name:<24} {best[0]:>11.2f} ms {best[1]:>9.2f} ms {best[2]:>7.2f} ms {best[3]:>6}')


if __name__ == '__main__':
    main()
