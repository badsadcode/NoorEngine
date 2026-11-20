"""Pictures of the built-in water presets for the editor's Water panel (Milestone 8D, part 7).

    python tools/samples/render_water_thumbnails.py

Reads the presets from the engine itself (noor_water_presets in noor_editor_api.dll, so the pictures always show what
the engine ships), draws each as the endless water of the render test scene Water_Shore (a sandy coast, three posts in
the shallows, foam, wet sand) with noor_player, and writes editor/NoorEditor/Shell/Water/<preset>.png (352 x 198), which
the editor embeds. Run it again after changing a preset. Needs a build and Pillow.
"""
import ctypes
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / 'editor' / 'NoorEditor' / 'Shell' / 'Water'


def find(name, under):
    found = sorted(under.rglob(name), key=lambda p: p.stat().st_mtime, reverse=True)
    if not found:
        sys.exit(f'{name} not found under {under}: build first')
    return found[0]


def presets():
    dll_path = find('noor_editor_api.dll', ROOT / 'build' / 'ninja' / 'tests')
    os.add_dll_directory(str(dll_path.parent))
    dll = ctypes.CDLL(str(dll_path))
    dll.noor_water_presets.restype = ctypes.c_char_p
    return json.loads(dll.noor_water_presets().decode('utf-8'))


def main():
    player = find('noor_player.exe', ROOT / 'build' / 'ninja' / 'tools')
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        project = pathlib.Path(tmp) / 'project'
        shutil.copytree(ROOT / 'tests' / 'data' / 'render_scenes', project, ignore=shutil.ignore_patterns('.noor'))
        base = json.loads((project / 'Scenes' / 'Water_Shore.noorscene').read_text(encoding='utf-8'))
        for preset in presets():
            scene = json.loads(json.dumps(base))
            scene['name'] = preset['name']
            for e in scene['entities']:
                if 'water' in e['components']:
                    e['components']['water'] = {'shape': 'sea', 'preset': preset['name']}
            path = project / 'Scenes' / 'Thumbnail.noorscene'
            path.write_text(json.dumps(scene, indent=2), encoding='utf-8')
            shot = pathlib.Path(tmp) / 'shot.bmp'
            subprocess.run([str(player), str(project), '--scene', 'Scenes/Thumbnail.noorscene', '--frames', '30', '--no-audio',
                            '--no-input', '--screenshot', str(shot), '--log', str(pathlib.Path(tmp) / 'player.log')], check=True)
            Image.open(shot).convert('RGB').resize((352, 198), Image.LANCZOS).save(OUT / f"{preset['name']}.png", optimize=True)
            print('wrote', OUT / f"{preset['name']}.png")


if __name__ == '__main__':
    main()
