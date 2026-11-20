"""What water costs (Milestone 8D, parts 1 and 2): Hillside's lake, the test pool and the wavy sea, with and without their water.

    python tools/samples/measure_hillside_lake.py

Draws samples/Hillside/Scenes/Hillside_Lake.noorscene (tools/samples/add_hillside_lake.py) and
tests/data/render_scenes/Scenes/Water_Pool.noorscene and Water_Waves.noorscene (part 2: the Sea preset) for 300 frames with noor_player, each as it is and with its water
entity taken out, three runs each, and prints the fastest GPU frame (the work itself: a desktop GPU that idles clocked
down and is shared makes medians swing) and the fastest Scene, Scene copy and See-through passes
(water and see-through draws), each the fastest frame's. Works on temporary copies, so nothing is written to the projects. Needs a build.
"""
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = [(ROOT / 'samples' / 'Hillside', 'Scenes/Hillside_Lake.noorscene', 'Lake'),
         (ROOT / 'tests' / 'data' / 'render_scenes', 'Scenes/Water_Pool.noorscene', 'Water'),
         (ROOT / 'tests' / 'data' / 'render_scenes', 'Scenes/Water_Waves.noorscene', 'Sea')]
RUNS = 3


def find(name, under):
    found = sorted(under.rglob(name), key=lambda p: p.stat().st_mtime, reverse=True)
    if not found:
        sys.exit(f'{name} not found under {under}: build first')
    return found[0]


def measure(player, project, scene, tmp):
    best = {}
    for _ in range(RUNS):
        report = pathlib.Path(tmp) / 'report.json'
        r = subprocess.run([str(player), str(project), '--scene', scene, '--frames', '300', '--no-audio', '--no-input', '--report', str(report),
                            '--log', str(pathlib.Path(tmp) / 'player.log')], capture_output=True)
        if r.returncode != 0 or not report.exists():
            return None
        data = json.loads(report.read_text(encoding='utf-8'))
        t = data['timing_ms']
        values = {'GPU': t['gpu']['min'], 'Scene': t['scene_gpu']['min'], 'Scene copy': t['scene_copy_gpu']['min'], 'See-through': t['see_through_gpu']['min']}
        for k, v in values.items():
            best[k] = min(best.get(k, v), v)
        report.unlink()
    return best


def main():
    player = find('noor_player.exe', ROOT / 'build' / 'ninja' / 'tools')
    print(f"{'scene':<28} {'GPU (fastest)':>14} {'Scene':>9} {'Scene copy':>11} {'See-through':>12}")
    with tempfile.TemporaryDirectory() as tmp:
        for source, scene, water_name in CASES:
            project = pathlib.Path(tmp) / source.name
            if not project.exists():
                shutil.copytree(source, project)  # with its .noor cache (Hillside's cooked forest)
            data = json.loads((project / scene).read_text(encoding='utf-8'))
            data['entities'] = [e for e in data['entities'] if e['name'] != water_name]
            dry = scene.replace('.noorscene', '_Dry.noorscene')
            (project / dry).write_text(json.dumps(data, indent=2), encoding='utf-8')
            for s in (scene, dry):
                best = measure(player, project, s, tmp)
                name = pathlib.Path(s).stem
                if best is None:
                    print(f'{name:<28} FAILED')
                    continue
                print(f"{name:<28} {best['GPU']:>11.2f} ms {best['Scene']:>6.2f} ms {best['Scene copy']:>8.3f} ms {best['See-through']:>9.3f} ms")


if __name__ == '__main__':
    main()
