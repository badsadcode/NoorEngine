"""What each sky feature costs on the Hillside sample (Milestone 8A, part 8: the 8A review's performance notes).

    python tools/samples/measure_hillside_skies.py

Draws each scene of samples/Hillside/Scenes/Presets (tools/samples/add_hillside_presets.py) and the original Hillside
(an HDRI sky) for 300 frames with noor_player, and prints each one's fastest GPU frame (the work itself: a desktop GPU
that idles clocked down and is shared makes medians swing) with its Environment and Scene passes. Adds one scene of its
own: Fair Weather with its cloud layer as a volume. Works on a temporary copy of the sample, so nothing is written to it.
Needs a build (cmake --build --preset ninja-release).
"""
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SAMPLE = ROOT / 'samples' / 'Hillside'


def find(name, under):
    found = sorted(under.rglob(name), key=lambda p: p.stat().st_mtime, reverse=True)
    if not found:
        sys.exit(f'{name} not found under {under}: build first')
    return found[0]


def main():
    player = find('noor_player.exe', ROOT / 'build' / 'ninja' / 'tools')
    with tempfile.TemporaryDirectory() as tmp:
        project = pathlib.Path(tmp) / 'Hillside'
        shutil.copytree(SAMPLE, project)
        # Fair Weather with its clouds as a volume (part 7b).
        env = json.loads((project / 'Assets' / 'Environments' / 'Fair Weather.noorenv').read_text(encoding='utf-8'))
        env['environment'].update({'cloud_type': 'volume', 'cloud_thickness': 1500})
        env['name'] = 'Fair Weather (volume)'
        env.pop('preset', None)
        (project / 'Assets' / 'Environments' / 'Fair Weather Volume.noorenv').write_text(json.dumps(env, indent=2), encoding='utf-8')
        scene = json.loads((project / 'Scenes' / 'Presets' / 'Hillside_FairWeather.noorscene').read_text(encoding='utf-8'))
        for e in scene['entities']:
            if 'environment' in e['components']:
                # By path through an inline copy: the new file has no id until a scan, and inline settings need none.
                e['components']['environment'] = env['environment']
        (project / 'Scenes' / 'Presets' / 'Hillside_FairWeatherVolume.noorscene').write_text(json.dumps(scene, indent=2), encoding='utf-8')

        scenes = ['Scenes/Hillside.noorscene'] + sorted(f'Scenes/Presets/{p.name}' for p in (project / 'Scenes' / 'Presets').glob('*.noorscene'))
        print(f"{'scene':<40} {'GPU (fastest)':>14} {'Environment':>12} {'Scene':>8}")
        for s in scenes:
            report = pathlib.Path(tmp) / 'report.json'
            r = subprocess.run([str(player), str(project), '--scene', s, '--frames', '300', '--no-audio', '--no-input', '--report', str(report),
                                '--log', str(pathlib.Path(tmp) / 'player.log')], capture_output=True)
            if r.returncode != 0 or not report.exists():
                print(f'{s:<40} FAILED')
                continue
            data = json.loads(report.read_text(encoding='utf-8'))
            gpu = data['timing_ms']['gpu']['min']
            passes = data.get('gpu_passes', {})
            print(f"{pathlib.Path(s).stem:<40} {gpu:>11.2f} ms {passes.get('Environment', 0):>9.2f} ms {passes.get('Scene', 0):>5.2f} ms")
            report.unlink()


if __name__ == '__main__':
    main()
