# NoorEngine

**Bad Ideas Works** · an independent, open-source (MIT) game engine, editor and toolset.

> We are building our own engine. We have excellent reasons. They remain classified.

## Honest status

**Milestone 0: foundation.** What exists today:

| Piece | Status |
|---|---|
| Build (CMake + MSVC), pinned dependencies | Works on Windows 10/11 |
| `foundation`: logging, errors (`Result<T>`), asserts, GUIDs | Works, unit-tested |
| `platform`: SDL3 window + events | Works |
| `render`: Vulkan 1.3 device, swapchain, clear-screen frame loop | Works, smoke-tested with validation layers (resize and maximize tested by hand; minimize untested; see CHANGELOG limitations) |
| `noor_diag`: diagnostics app (window + GPU info + purple clear) | Works |
| Editor, scenes, assets, scripting, everything else | **Not started.** See the [master spec](docs/NoorEngine_Design_Package/NOORENGINE_MASTER_SPEC.md) |

All editor images in `docs/` are **concept art**, not screenshots.

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

Options: `--frames N` (exit after N frames), `--no-validation`, `--log <file>`.

Tests labelled `gpu` need a Vulkan 1.3 GPU; skip them with `ctest --preset debug -LE gpu`.

## Repository map

| Path | What |
|---|---|
| `engine/foundation` | Logging, errors, asserts, IDs. Depends on nothing. |
| `engine/platform` | OS layer (windows, events) on SDL3. |
| `engine/render` | Vulkan backend. |
| `tools/noor_diag` | Diagnostics app; permanent developer utility. |
| `tests/` | Unit and smoke tests (doctest + CTest). |
| `docs/decisions` | Architecture Decision Records. Read these before arguing. 😉 |
| `docs/devlog` | The public devlog. Funny, never fake. |
| `IDEAS.md` | The idea parking lot. Throw ideas here. |

## License

Code: [MIT](LICENSE). Third-party components: see [`third_party/manifest.json`](third_party/manifest.json).
Concept art and logos in `docs/` are not covered by the MIT license until decided otherwise.

*Code shall remain understandable to whichever AI inherits this mess.*
