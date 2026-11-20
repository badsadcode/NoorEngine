"""The FBX research run (Milestone 8C, part 7): each FBX read twice, by Blender's importer and by the engine's, compared.

  python tools/samples/fbx_research.py --blender <blender.exe> --cli <noor_cli.exe> --out <folder> <files or folders...>

For every .fbx: Blender (tools/samples/make_fbx_test_files.py "reference") writes what it read as a GLB; noor_cli
compare-models compares the engine's import of the FBX with its import of that GLB. Writes <out>/fbx_research.json and
<out>/fbx_research.md: per file, what wrote it (the FBX's Creator), its skeleton, meshes and clips as each side read
them, and how far apart they are. Nothing is copied: files that can't be committed are only read where they are.
"""
import argparse
import json
import pathlib
import re
import subprocess
import time

HERE = pathlib.Path(__file__).resolve().parent


def creator(path):
    with open(path, "rb") as f:
        head = f.read(200000)
    text = head.decode("latin-1")
    for pattern in (r"FBX SDK/FBX Plugins version [0-9.]+", r"Blender \(stable FBX IO\)[^\x00]{0,24}", r"Blender[^\x00]{0,30}"):
        m = re.search(pattern, text)
        if m:
            return re.sub(r"[^\x20-\x7e]", "", m.group(0)).strip(" -")
    return "binary" if head.startswith(b"Kaydara FBX Binary") else "ASCII FBX"


def files(args):
    for item in args:
        p = pathlib.Path(item)
        if p.is_dir():
            yield from sorted(x for x in p.rglob("*") if x.suffix.lower() == ".fbx" and ".claude" not in x.parts)
        elif p.suffix.lower() == ".fbx":
            yield p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--blender", required=True)
    ap.add_argument("--cli", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--reuse", action="store_true", help="keep Blender's GLBs from an earlier run in --out")
    ap.add_argument("paths", nargs="+")
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    for i, fbx in enumerate(files(a.paths)):
        row = {"file": str(fbx), "bytes": fbx.stat().st_size, "creator": creator(fbx)}
        ref = out / f"{i:03d}_{fbx.stem}.ref.glb"
        report = out / f"{i:03d}_{fbx.stem}.json"
        t0 = time.time()
        b = None if a.reuse and ref.exists() else subprocess.run([a.blender, "-b", "--factory-startup", "-P", str(HERE / "make_fbx_test_files.py"), "--", "reference", str(fbx), str(ref)],
                           capture_output=True, text=True, errors="replace")
        row["blender_s"] = round(time.time() - t0, 1)
        if not ref.exists():
            row["error"] = "Blender couldn't read it: " + ((b.stdout + b.stderr)[-400:].strip() if b else "")
            rows.append(row)
            print(fbx.name, "BLENDER FAILED")
            continue
        t0 = time.time()
        c = subprocess.run([a.cli, "compare-models", str(fbx), str(ref), "--report", str(report)], capture_output=True, text=True, errors="replace")
        row["compare_s"] = round(time.time() - t0, 1)
        if c.returncode != 0 or not report.exists():
            row["error"] = "engine: " + (c.stdout + c.stderr)[-400:].strip()
            rows.append(row)
            print(fbx.name, "ENGINE FAILED")
            continue
        r = json.loads(report.read_text(encoding="utf-8"))
        row.update({k: r[k] for k in ("nodes_a", "nodes_b", "matched", "triangles_a", "triangles_b", "skins_a", "skins_b", "joints_a", "joints_b",
                                       "clips_a", "clips_b", "rest_joints_m", "rest_shape_m", "worst_joints_m", "worst_shape_m", "warnings_a")})
        row["only_a"], row["only_b"] = len(r["only_a"]), len(r["only_b"])
        row["clips_compared"] = len(r["clips"])
        rows.append(row)
        print(f"{fbx.name}: joints {row['worst_joints_m'] * 1000:.3f} mm, shape {row['worst_shape_m'] * 1000:.3f} mm")
    (out / "fbx_research.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
    lines = ["| File | Written by | Nodes (ours / Blender, matched) | Triangles | Joints | Clips | Joints within | Shape within | Notes |", "|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        if "error" in r:
            lines.append(f"| {pathlib.Path(r['file']).name} | {r['creator']} | | | | | | | {r['error'][:120]} |")
            continue
        lines.append(f"| {pathlib.Path(r['file']).name} | {r['creator']} | {r['nodes_a']} / {r['nodes_b']}, {r['matched']} | {r['triangles_a']} / {r['triangles_b']} | "
                     f"{r['joints_a']} / {r['joints_b']} | {r['clips_a']} / {r['clips_b']} | {r['worst_joints_m'] * 1000:.3f} mm | {r['worst_shape_m'] * 1000:.3f} mm | "
                     f"{'; '.join(r['warnings_a'])[:160]} |")
    (out / "fbx_research.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("wrote", out / "fbx_research.md")


main()
