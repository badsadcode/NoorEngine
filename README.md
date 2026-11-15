# NoorEngine

[![CI](https://github.com/badsadcode/NoorEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/badsadcode/NoorEngine/actions/workflows/ci.yml)

**Bad Ideas Works** · an independent, open-source (MIT) game engine, editor and toolset.

> We are building our own engine. We have excellent reasons. They remain classified.

**Website:** [badsadcode.github.io/NoorEngine](https://badsadcode.github.io/NoorEngine/), the devlog and
this status table, generated from `docs/` (source in [`website/`](website/)).

## Honest status

**Milestones 0 and 1 done** ([M1 review](docs/milestones/M1_REVIEW.md)); **Milestone 2 (projects, assets, scenes) in progress.** What exists today:

| Piece | Status |
|---|---|
| Build (CMake + MSVC), pinned dependencies | Works on Windows 10/11 |
| `foundation`: logging, errors (`Result<T>`), asserts, GUIDs, math | Works, unit-tested |
| `platform`: SDL3 window, events, window control (minimize/restore/resize/move to display), live redraw while resizing | Works; automated lifecycle test |
| `platform`: input (keyboard, mouse, relative mouse mode) | Works; used by the fly camera |
| `render`: Vulkan 1.3 device, swapchain, depth buffer, meshes, lit mesh pipeline, camera, fly camera, GPU timing, frame capture | Works; smoke, GPU pixel, 2,500-cube stress and window lifecycle tests with validation; debug labels for RenderDoc. Device loss reported, not recovered; high-DPI >100% untested |
| `noor_diag`: diagnostics app (fly camera, `--cubes N` stress grid, timings, `--verify-render`, `--screenshot`) | Works |
| `editor_api`: C API DLL hosting engine viewports in a foreign window (v0) | Works in the Avalonia spike; API not stable |
| Editor GUI stack: Avalonia + Dock ([ADR-003](docs/decisions/ADR-003-editor-gui-framework.md)) | **Spike only**, decision accepted; NoorEditor itself not started |
| `asset`: projects, `.noormeta` GUIDs, registry (survives renames/moves), GLB import with Noor Interchange data | Works, unit-tested with a real BadBuilding block |
| `scene`: entities + components (layers, tags), deterministic save/load, format migrations, undo/redo, model instancing, validation | Works, unit-tested (incl. randomized undo/redo invariant) |
| `noor_cli`: headless project tool (`new`, `scan`, `info`, `add-model`, `validate`, `migrate`) | Works; runs in CI |
| Prefabs, textures, scripting, everything else | **Not started.** See the [master spec](docs/NoorEngine_Design_Package/NOORENGINE_MASTER_SPEC.md) |

Editor images in `docs/NoorEngine_Design_Package` are **concept art**. Screenshots in
`docs/devlog/img` are real captures of the engine and the editor spike.

## Build

Prerequisites: Windows 10/11, Visual Studio 2022 (Desktop C++ workload), CMake ≥ 3.28,
[Vulkan SDK](https://vulkan.lunarg.com/) 1.3+ (sets `VULKAN_SDK`), Git.

```bash
cmake --preset windows
cmake --build --preset debug
ctest --preset debug
```

Run the diagnostics app:

```bash
build/windows/tools/noor_diag/Debug/noor_diag.exe
```

Controls: hold right mouse to look, WASD to move, Q/E down/up, Shift faster, wheel for speed, Esc quits.
Options: `--frames N` (exit after N frames), `--cubes N`, `--no-validation`, `--verify-render`,
`--lifecycle-test`, `--glb <file.glb>` (hot-reloads when the file changes), `--screenshot <file.bmp>`, `--log <file>`.

Tests labelled `gpu` need a Vulkan 1.3 GPU; skip them with `ctest --preset debug -LE gpu`.
CI (GitHub Actions) builds Debug and Release on every push and pull request and runs everything
except the `gpu` tests, because GitHub's runners have no GPU. Run those locally before pushing
renderer changes. CI uses the `windows-ninja` preset (Ninja + whatever MSVC the runner has).

## Repository map

| Path | What |
|---|---|
| `engine/foundation` | Logging, errors, asserts, IDs. Depends on nothing. |
| `engine/platform` | OS layer (windows, events) on SDL3. |
| `engine/render` | Vulkan backend. |
| `engine/editor_api` | C ABI DLL the C# editor uses to host engine viewports. |
| `engine/asset`, `engine/scene` | Projects, asset registry, GLB import; scene documents. |
| `tools/noor_diag` | Diagnostics app; permanent developer utility. |
| `tools/noor_cli` | Headless project tool. |
| `spikes/` | Technology experiments that decide ADRs. Not product code. |
| `tests/` | Unit and smoke tests (doctest + CTest). |
| `docs/decisions` | Architecture Decision Records. Read these before arguing. 😉 |
| `docs/devlog` | The public devlog. Funny, never fake. |
| `IDEAS.md` | The idea parking lot. Throw ideas here. |

## License

Code: [MIT](LICENSE). Third-party components: see [`third_party/manifest.json`](third_party/manifest.json).
Concept art and logos in `docs/` are not covered by the MIT license until decided otherwise.

*Code shall remain understandable to whichever AI inherits this mess.*
