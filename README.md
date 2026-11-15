# NoorEngine

[![CI](https://github.com/badsadcode/NoorEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/badsadcode/NoorEngine/actions/workflows/ci.yml)

**Bad Ideas Works** · an independent, open-source (MIT) game engine, editor and toolset.

> We are building our own engine. We have excellent reasons. They remain classified.

**Website:** [badsadcode.github.io/NoorEngine](https://badsadcode.github.io/NoorEngine/), the devlog and
this status table, generated from `docs/` (source in [`website/`](website/)).

## Honest status

**Milestone 0 done; Milestone 1 (window, GPU, first real viewport) in progress.** What exists today:

| Piece | Status |
|---|---|
| Build (CMake + MSVC), pinned dependencies | Works on Windows 10/11 |
| `foundation`: logging, errors (`Result<T>`), asserts, GUIDs, math | Works, unit-tested |
| `platform`: SDL3 window + events | Works |
| `render`: Vulkan 1.3 device, swapchain, depth buffer, meshes, lit mesh pipeline, camera, frame capture | Works; smoke test + GPU pixel test with validation layers (resize/maximize tested by hand; minimize untested) |
| `noor_diag`: diagnostics app (GPU info + lit spinning cube, `--verify-render`, `--screenshot`) | Works |
| `editor_api`: C API DLL hosting engine viewports in a foreign window (v0) | Works in the Avalonia spike; API not stable |
| Editor GUI stack: Avalonia + Dock ([ADR-003](docs/decisions/ADR-003-editor-gui-framework.md)) | **Spike only**, decision accepted; NoorEditor itself not started |
| Scenes, assets, scripting, everything else | **Not started.** See the [master spec](docs/NoorEngine_Design_Package/NOORENGINE_MASTER_SPEC.md) |

Editor images in `docs/NoorEngine_Design_Package` are **concept art**. Screenshots in
`docs/devlog/img` are real captures of the spike.

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

Options: `--frames N` (exit after N frames), `--no-validation`, `--verify-render`,
`--screenshot <file.bmp>`, `--log <file>`.

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
| `tools/noor_diag` | Diagnostics app; permanent developer utility. |
| `spikes/` | Technology experiments that decide ADRs. Not product code. |
| `tests/` | Unit and smoke tests (doctest + CTest). |
| `docs/decisions` | Architecture Decision Records. Read these before arguing. 😉 |
| `docs/devlog` | The public devlog. Funny, never fake. |
| `IDEAS.md` | The idea parking lot. Throw ideas here. |

## License

Code: [MIT](LICENSE). Third-party components: see [`third_party/manifest.json`](third_party/manifest.json).
Concept art and logos in `docs/` are not covered by the MIT license until decided otherwise.

*Code shall remain understandable to whichever AI inherits this mess.*
