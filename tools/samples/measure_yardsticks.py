"""Measures Milestone 9's yardsticks (9A part 1) and writes their baseline.

    python tools/samples/measure_yardsticks.py [--runs 3] [--check]

Runs each yardstick project in noor_player (RelWithDebInfo) for ten seconds, three times, and takes the median of each
number: the CPU per frame (the game, the view and recording, vsync aside), the GPU per frame, memory, threads and the
time to the first frame. Writes tests/yardsticks.json (each measurement and its limit: the measurement plus a margin for
a busy machine) and prints the table for docs/milestones/M9_YARDSTICKS.md. With --check it only compares a fresh
measurement with the file already there.

Hillside needs its graphs cooked first (noor_cli cook samples/Hillside; the test suite's hillside_cooked fixture does it).
"""
import argparse
import json
import pathlib
import statistics
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
PLAYER = ROOT / "build" / "ninja" / "tools" / "noor_player" / "RelWithDebInfo" / "noor_player.exe"
BASELINE = ROOT / "tests" / "yardsticks.json"
FRAMES = 600

PROJECTS = {
    "cube": ("tests/data/yardstick_cube", "Scenes/Main.noorscene", "A single cube in an otherwise empty scene"),
    "pong": ("samples/NoorPong", "Scenes/Pong.noorscene", "NoorPong: 2D, an orthographic camera, a sprite, Lua"),
    "fps": ("samples/NoorFPS-lite", "Scenes/Main_Lua.noorscene", "NoorFPS-lite: a compact hand-made 3D level, physics, Lua"),
    "hillside": ("samples/Hillside", "Scenes/Hillside.noorscene", "Hillside: a procedural landscape (graph terrain, scatter)"),
}

# measured key: (path in the report, limit = value * scale + margin)
METRICS = {
    "cpu_median_ms": (("timing_ms", "cpu", "median"), 1.3, 0.2),
    "cpu_p95_ms": (("timing_ms", "cpu", "p95"), 1.5, 0.4),
    "gpu_median_ms": (("timing_ms", "gpu", "median"), 1.3, 0.1),
    "working_set_mb": (("process", "working_set_mb"), 1.15, 24.0),
    "private_mb": (("process", "private_mb"), 1.15, 24.0),
    "gpu_resident_mb": (("process", "gpu_resident_mb"), 1.05, 8.0),
    "threads": (("process", "threads"), 1.0, 2.0),
    "first_frame_s": (("process", "first_frame_s"), 1.5, 0.5),
}


def run(project, scene, work):
    report = work / "report.json"
    subprocess.run([str(PLAYER), str(ROOT / project), "--scene", scene, "--frames", str(FRAMES), "--no-audio", "--no-input",
                    "--report", str(report), "--log", str(work / "player.log")], check=True, capture_output=True)
    r = json.loads(report.read_text(encoding="utf-8"))
    out = {}
    for key, (path, _, _) in METRICS.items():
        v = r
        for p in path:
            v = v[p]
        out[key] = v
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    results = {}
    with tempfile.TemporaryDirectory() as tmp:
        for name, (project, scene, _) in PROJECTS.items():
            runs = [run(project, scene, pathlib.Path(tmp)) for _ in range(a.runs)]
            results[name] = {k: statistics.median(r[k] for r in runs) for k in METRICS}
            print(f"{name}: " + ", ".join(f"{k} {v:.3f}" for k, v in results[name].items()), file=sys.stderr)
    if a.check:
        base = json.loads(BASELINE.read_text(encoding="utf-8"))["projects"]
        bad = [(n, k) for n in results for k in METRICS if results[n][k] > base[n]["limits"][k]]
        print("over:" if bad else "all within limits", bad if bad else "")
        sys.exit(1 if bad else 0)
    out = {"about": "Milestone 9's yardsticks (9A part 1): what small projects cost before big worlds arrive. Written by "
                    "tools/samples/measure_yardsticks.py; checked by tests/yardstick_check.cmake.",
           "machine": "NVIDIA RTX 3080, Windows 10, 1280x720, RelWithDebInfo",
           "projects": {}}
    for name, (project, scene, about) in PROJECTS.items():
        m = results[name]
        limits = {}
        for k, (_, scale, margin) in METRICS.items():
            v = m[k] * scale + margin
            limits[k] = round(v) if k == "threads" else round(v, 3)
        out["projects"][name] = {"about": about, "project": project, "scene": scene, "frames": FRAMES,
                                 "measured": {k: (round(v) if k == "threads" else round(v, 3)) for k, v in m.items()},
                                 "limits": limits}
    BASELINE.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8", newline="\n")
    print("| Yardstick | CPU/frame median (p95) | GPU/frame | Working set | Private | GPU resident | Threads | First frame |")
    print("|---|---|---|---|---|---|---|---|")
    for name, (_, _, about) in PROJECTS.items():
        m = results[name]
        print(f"| {about} | {m['cpu_median_ms']:.2f} ms ({m['cpu_p95_ms']:.2f}) | {m['gpu_median_ms']:.2f} ms | "
              f"{m['working_set_mb']:.0f} MB | {m['private_mb']:.0f} MB | {m['gpu_resident_mb']:.0f} MB | {m['threads']:.0f} | "
              f"{m['first_frame_s']:.2f} s |")


if __name__ == "__main__":
    main()
