# NoorEngine

[![CI](https://github.com/badsadcode/NoorEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/badsadcode/NoorEngine/actions/workflows/ci.yml)

**Bad Ideas Works** · an independent, open-source (MIT) game engine, editor and toolset.

> We are building our own engine. We have excellent reasons. They remain classified.

**Website:** [badsadcode.github.io/NoorEngine](https://badsadcode.github.io/NoorEngine/), the devlog and
this status table, generated from `docs/` (source in [`website/`](website/)).

## Honest status

**Milestones 0–3 done** ([M1](docs/milestones/M1_REVIEW.md), [M2](docs/milestones/M2_REVIEW.md), [M3](docs/milestones/M3_REVIEW.md) reviews); **Milestone 4 (scripts and gameplay) in progress** ([plan](docs/milestones/M4_PLAN.md)). What exists today:

| Piece | Status |
|---|---|
| Build (CMake + MSVC), pinned dependencies | Works on Windows 10/11 |
| `foundation`: logging, errors (`Result<T>`), asserts, GUIDs, math | Works, unit-tested |
| `platform`: SDL3 window, events, window control (minimize/restore/resize/move to display), live redraw while resizing | Works; automated lifecycle test |
| `platform`: input (keyboard, mouse, relative mouse mode) | Works; used by the fly camera |
| `render`: Vulkan 1.3 device, swapchain, depth buffer, meshes, lit mesh pipeline, camera, fly camera, GPU timing, frame capture | Works; smoke, GPU pixel, 2,500-cube stress and window lifecycle tests with validation; debug labels for RenderDoc. Device loss reported, not recovered; high-DPI >100% untested |
| `noor_diag`: diagnostics app (fly camera, `--cubes N` stress grid, timings, `--verify-render`, `--screenshot`) | Works |
| `editor_api`: C API DLL (v9): engine viewports in a foreign window + editable documents ([ADR-025](docs/decisions/ADR-025-editor-engine-boundary.md)) | Works; API tests run in CI. Not a stable plugin ABI yet (ADR-007) |
| **NoorEditor** ([editor/NoorEditor](editor/NoorEditor)): project hub, docked viewport (click to select, fly camera, move/rotate/scale gizmos with snapping, highlight), scene tree, inspector (name/transform/components), content browser with live reload, console, preferences, play/stop, undo/redo, save, remembered layout | **Milestone 3 complete.** Works; 61-check end-to-end smoke test drives real clicks, drags and keys (GPU, local). HiDPI checked by hand at 150%; no multi-select |
| `asset`: projects, `.noormeta` GUIDs, registry (survives renames/moves), GLB import with Noor Interchange data | Works, unit-tested with a real BadBuilding block |
| `scene`: entities + components (layers, tags), prefabs (overrides, nesting, variants), deterministic save/load, format migrations, undo/redo, validation | Works, unit-tested (incl. randomized undo/redo invariant) |
| `noor_cli`: headless project tool (`new`, `scan`, `info`, `add-model`, `make-prefab`, `add-prefab`, `validate`, `migrate`, `build`) | Works; runs in CI |
| `noor_player` + `noor_cli build` + editor **File ▸ Build Game**: a game as a folder with its own .exe, no editor needed ([ADR-028](docs/decisions/ADR-028-player-and-builds.md)) | **Milestone 4, part 6.** Works, with Lua and C# scripts; tested from a project and from builds (GPU, local). Needs the Visual C++ and .NET 10 runtimes on the PC |
| `runtime`: game world, frame order, fixed 60 Hz step, input actions/axes, built-in behaviours; the editor's play mode runs it with keyboard input from the viewport | **Milestone 4, part 1.** Works, unit-tested; physics and audio plug in next |
| `scripting`: **Lua (Luau)** and **C#** scripts with one API: entities, transforms, input, components, physics, character, sound, HUD, logging ([Lua](docs/scripting/LUA_API.md), [C#](docs/scripting/CSHARP_API.md)) | **Milestone 4, parts 2 and 4.** Works; C# compiles on Play and reloads after edits |
| `physics`: **Jolt** rigid bodies, colliders (box, sphere, capsule, mesh from GLBs), raycasts, forces, collision/trigger events; **character controller**; game camera; mouse capture; primitive shapes | **Milestone 4, part 3.** Works; unit-tested; a first-person player walks and jumps in the editor |
| Environments: HDRI or procedural sky, image-based lighting (prefiltered cubemap, SH9, BRDF table), sky background | **Milestone 5, part 5.** Works; `.hdr` only, no reflection probes |
| Shadows and lights: cascaded sun shadows (stable, PCF), point and spot lights in lumens | **Milestone 5, part 4.** Works; no shadows from lamps yet |
| Materials: `.noormat` (glTF channels, instances), texture set → material, GLB materials and embedded textures, GPU material table, hot reload | **Milestone 5, part 3.** Works; no see-through blending yet |
| Rendering quality: 4× MSAA, see-through (blend) materials, per-cascade shadow culling, exposure presets, Render stats panel, measurements | **Milestone 5, part 7.** Works ([measurements](docs/rendering/MEASUREMENTS.md)) |
| Texture compression: BC7 (colour, data), BC5 (normals), import settings tab in the editor | **Milestone 5.** Works; 4× less texture memory; no BC6H/BC4, not cooked into built games yet |
| PBR-Showroom sample (sun + 3 HDRIs, roughness sweeps, textured sets) as render tests; NoorFPS-lite textured; [Milestone 5 review](docs/milestones/M5_REVIEW.md) | **Milestone 5 complete** (with documented exceptions: no material graph, bloom, reflection probes) |
| CSG brushes: box, wedge, cylinder, cone, sphere, stairs, arch; add/subtract/intersect (Manifold); Create ▸ Brush, reorder, see-through cutters; materials and UVs per face (click a face); cooked for games with mesh collision; Noor Interchange GLB export | **Milestone 6, parts 1–4.** Works in the editor and in built games |
| Material editor: live preview (shapes, HDRIs, exposure), every field, instances with overrides, saves as you edit, undo | **Milestone 5, part 6.** Works; no thumbnails or node graph |
| Textures: PNG/JPG/TGA/BMP/HDR import with colour-space kinds, linear-light mipmaps, cache; bindless texture table; base colour and normal maps on primitives | **Milestone 5, part 2.** Works; uncompressed, no GLB textures yet |
| Lighting: HDR frame, physical units (lux, nits, EV100), GGX PBR core, sun `light` component, Khronos PBR Neutral / ACES tonemapping, golden-image render tests | **Milestone 5, part 1.** Works; no materials, textures or shadows yet |
| Sample: **NoorFPS-lite** (`samples/NoorFPS-lite`), a target shooter in C# with a Lua twin, public API only | **Milestone 4, part 8.** Works; built and its demo played by ctest |
| NoorCode MVP: built-in script editor (tabs, Lua/C# highlighting, find, error lines marked and linked from the Console); AvaloniaEdit | **Milestone 4, part 7b.** Works; editor smoke checks. No completion or debugger yet |
| HUD: scripts draw text and rectangles over the game (`noor.ui` / `Ui`); overlay pass, distance-field font (Atkinson Hyperlegible, OFL) | **Milestone 4, part 7a.** Works; pixel-checked in the player tests |
| `audio`: **miniaudio** sound sources, listener, buses (master/music/sfx/ui/voice), 3D attenuation, one-shots; WAV/FLAC/MP3 assets; script API | **Milestone 4, part 5.** Works; tested by measuring the mixed samples (no device needed) |
| Textures, standalone builds, everything else | **Not started.** See the [master spec](docs/NoorEngine_Design_Package/NOORENGINE_MASTER_SPEC.md) |

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
`--lifecycle-test`, `--glb <file.glb>` or `--project <dir> --scene <Scenes/x.noorscene>` (both hot-reload), `--screenshot <file.bmp>`, `--log <file>`.

Run a project without the editor, or build it into a folder you can share:

```bash
build/ninja/tools/noor_player/Debug/noor_player.exe tests/data/player_project
```

```bash
build/ninja/tools/noor_cli/RelWithDebInfo/noor_cli.exe build tests/data/player_project build/player_test_game
```

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
| `engine/editor_api` | C ABI DLL the C# editor uses: viewports and documents. |
| `editor/NoorEditor` | The editor (C#, Avalonia). |
| `engine/asset`, `engine/scene` | Projects, asset registry, GLB import; scene documents. |
| `engine/runtime` | The running game: world, systems, input, the scripting API. |
| `engine/physics` | Jolt physics as a runtime system. |
| `engine/scripting` | Script languages as runtime systems: Lua (Luau), and C# (`dotnet/NoorEngine.Scripting`). |
| `tools/noor_diag` | Diagnostics app; permanent developer utility. |
| `tools/noor_cli` | Headless project tool; `build` packs a game. |
| `tools/noor_player` | Runs a game outside the editor (what a built game's .exe is). |
| `engine/game` | What every running game needs (world setup, what to draw), shared by Play and the player. |
| `spikes/` | Technology experiments that decide ADRs. Not product code. |
| `tests/` | Unit and smoke tests (doctest + CTest). |
| `docs/decisions` | Architecture Decision Records. Read these before arguing. 😉 |
| `docs/devlog` | The public devlog. Funny, never fake. |
| `IDEAS.md` | The idea parking lot. Throw ideas here. |

## License

Code: [MIT](LICENSE). Third-party components: see [`third_party/manifest.json`](third_party/manifest.json).
Concept art and logos in `docs/` are not covered by the MIT license until decided otherwise.

*Code shall remain understandable to whichever AI inherits this mess.*
