# Changelog

Technical, honest, newest first. "Works" means tested; anything else says what it is.

## [Unreleased] — Milestone 1 in progress

### 2026-11-15 — Noor Interchange v0.2
Feedback from the first real exporter (BadBuilding 6d59174):
- Collision: `extras.noor.collision` is authoritative and never rendered; suffix `-colonly` /
  `-convcolonly` replaces `-col` (Godot draws `-col` meshes).
- LODs: one file per level (`<name>.lod<n>.glb`, schema role `lod` + `lod` number); wrapper
  nodes still allowed.
- Markers: new `passage`; `entrance` (place to get in) vs `door` (openable object) clarified;
  more `entrance` kinds; `door.opens`.
- Ids may be UUID v5 from a natural key; tool-specific manifest blocks; `not_run` validation allowed.
- Schema re-validated: examples pass, v0.1 manifests still accepted, LOD files without a level rejected.

### 2026-11-15 — Milestone 1, second increment: input, fly camera, GPU timing, stress test
**Added**
- `platform/input.h`: keyboard (physical scancodes) and mouse state with pressed/released edges,
  mouse deltas, wheel; released on focus loss. `platform::input()`, `set_relative_mouse()`.
- `render/fly_camera.h`: free-fly camera (yaw/pitch, flattened forward, clamped pitch,
  speed/boost); 7 unit tests.
- GPU timestamps per frame (`FrameStats::gpu_ms`) and CPU record time (`cpu_record_ms`).
- `ErrorCode::DeviceLost` with a plain-language message for lost devices (all Vulkan calls via
  `vk_failure`). Detection and reporting only; recovery not implemented.
- `noor_diag`: ground plane, fly camera controls, `--cubes N` grid, timings in the title and an
  exit summary. Verify mode unchanged (fixed camera, hero cube only).
- Test `stress.cubes_2500`: 2,502 draws/frame for 120 frames with validation. 35/35 tests pass
  (Ninja Debug build).

**Measured** (RTX 3080, 1280×720, 2,502 draws): Release CPU record 0.38 ms without validation,
9.0 ms with validation; GPU 0.06–0.09 ms.

**Known limitations**
- One draw call per object; no instancing yet.
- Minimize/restore and monitor changes still untested; device loss is reported, not recovered.

### 2026-11-15 — Continuous integration
**Added**
- `.github/workflows/ci.yml`: Windows, Debug + RelWithDebInfo builds, all tests except `gpu`-labelled
  ones (runners have no GPU), Avalonia spike build. Vulkan SDK from LunarG's installer, cached.
  Actions pinned to commit SHAs.
- `windows-ninja` CMake preset: CI finds the newest MSVC with `vswhere` and builds with Ninja, so
  it doesn't break when runner images change Visual Studio versions (the first run failed exactly
  that way: `windows-latest` has no VS 2022). Verified locally too: 27/27 tests with the Ninja preset.
- CI badge in the README.

**Known limitations**
- GPU tests run only on developer machines. A software Vulkan driver (e.g. lavapipe) could bring
  the render pixel test to CI later.

### 2026-11-15 — Noor Interchange v0.1 (draft)
**Added**
- `docs/interchange/NOOR_INTERCHANGE.md`: shared export rules for all Noor tools (NoorMD benches,
  NoorShip, NoorLevel, NoorArchitect, ...): GLB + `<name>.noor.json` manifest + recipe; markers,
  parts, LODs and collision in `extras.noor`; ADR-022 space conventions.
- `docs/interchange/noor.asset.schema.json` (JSON Schema 2020-12) and two example manifests.
  Examples validate; wrong units, Z-up, unknown kinds, escaping paths and missing ids are rejected.
- ADR-023 (Proposed).

**Known limitations**
- Draft: no tool exports it yet and NoorEngine has no importer yet (Milestone 2).

### 2026-11-15 — Milestone 1, first increment: meshes, camera, depth, GPU pixel tests
**Added**
- `noor/foundation/math.h`: Vec3/Vec4/Mat4, rotation, look_at, reverse-Z infinite perspective;
  7 unit tests locking in ADR-022 conventions (right-handed, +Y up, column-major, reverse-Z).
- ADR-022 (math and coordinate conventions).
- Build-time HLSL → SPIR-V with DXC (`cmake/NoorShaders.cmake`), embedded into the binary.
- VMA 3.5 for GPU memory.
- Renderer: dynamic rendering, D32 reverse-Z depth buffer, swapchain image views, mesh pipeline
  (lit vertex colors), `create_mesh`/`destroy_mesh` (staged upload, index validation),
  `FrameDesc` with camera and draw list, per-frame stats, frame capture (`request_capture` /
  `take_capture`). Renderer split into `vulkan_renderer.cpp`, `vulkan_resources.cpp`,
  `vulkan_capture.cpp` with a shared internal header.
- `noor_diag`: lit spinning cube, `--verify-render` (pixel probes) and `--screenshot` (BMP).
- Test `render.cube_pixels`: checks background, front face and top face colors in a real frame.
  Verified that it fails when triangle winding is deliberately broken. 27 tests pass.

**Known limitations**
- Mesh upload blocks on the GPU; mesh destruction waits for device idle.
- Single hard-coded shader and light; editor viewport still clears only.
- Not done yet for Milestone 1: input services, GPU timing markers, device-loss handling, stress tests.

## Milestone 0

### 2026-11-15 — Avalonia editor spike (ADR-003 accepted)
**Added**
- `platform::PresentationTarget`: interface the renderer presents to. `Window` (SDL) and the new
  `NativeChildWindow` (Win32 child window inside a foreign parent, e.g. the editor UI) implement it.
  The renderer no longer depends on SDL windows.
- `noor_editor_api.dll` (v0, spike-grade C ABI): log forwarding, viewports (native child window +
  dedicated render thread, live stats). Destroy pumps UI messages while joining the render thread.
- `spikes/avalonia_viewport`: Avalonia 12.1.4 + Dock 12.1.0.13 editor shell with a docked Vulkan
  viewport, live Inspector/Console/status bar, labeled sample panels, and `--smoke` automated checks.
- Devlog #003 with real screenshots.

**Measured** (Debug, validation on, RTX 3080, 60 Hz, 100% scale): 60 FPS; 236 ms viewport → first
frame; 1.6 s process start → first frame; resize reaches swapchain; float into own window re-renders
after 363 ms; 0 validation errors; 0.74% machine CPU; 247 MB working set. Details in ADR-003.

**Fixed during development**
- Spike screenshots first used a screen BitBlt, which captured another application overlapping the
  window. Deleted immediately; capture now uses `PrintWindow`, which only renders our own window.

**Tested by hand** (Tom): hover/click input reaches the viewport (hue follows the mouse, click
brightens); drag-and-drop docking with the mouse works.

**Known limitations**
- Untested: high-DPI >100%, dock drop indicators over the viewport itself (airspace), live
  drag-resize, keyboard focus, Release numbers.
- One Vulkan instance + device per viewport (floating costs ~360 ms); needs a shared device.
- Floating Dock windows show the default blue accent instead of the theme purple.
- `renderer.h` now includes `vulkan.h` indirectly through `presentation_target.h`, weakening
  ADR-002's "no Vulkan types above render/" rule. To be tidied when the RHI layer arrives.

### 2026-11-15 — Devlog website
**Added**
- `website/`: static site (home with the README "Honest status" table, devlog index, one page
  per `docs/devlog/DEVLOG_NNN_*.md`, ADR index, Atom feed, 404). Generated by a small Node script;
  one dependency, `marked` 18.1.2 (MIT), pinned with a lockfile. Dark-first with a light mode.
  Editor mockup shown only with a CONCEPT ART label.
- New devlog files appear on the site automatically; relative links are rewritten to site pages
  or GitHub. 12 unit tests for the build helpers (`npm test` in `website/`).
- `.github/workflows/pages.yml`: tests, builds and deploys to GitHub Pages on push to `main`
  (site, devlog, ADR index, README or workflow changes) and on manual run; PRs build only.

**Known limitations**
- Not yet deployed or run on GitHub Actions at the time of writing. Workflow YAML was not
  checked with a linter locally.

### 2026-11-15 — Repository foundation
**Added**
- Repository layout, MIT license (ADR-019), CONTRIBUTING, IDEAS parking lot, project CLAUDE.md.
- ADRs: 001 C++23/CMake/MSVC (Accepted), 002 Vulkan 1.3 (Accepted), 003 Avalonia editor GUI
  (Proposed, spike pending), 019 MIT license (Accepted), 021 SDL3 + dependency policy (Accepted).
- CMake build with presets; dependencies pinned by commit hash (SDL 3.4.18, doctest 2.5.3);
  `third_party/manifest.json`.
- `noor::foundation`: structured thread-safe logging (console, file, memory sinks; per-level
  counters), `Result<T>`/`Error` error handling, `NOOR_ASSERT` with pluggable handler, v4 GUIDs.
- `noor::platform`: SDL3 init/shutdown, event pump, high-DPI aware window, Vulkan surface bridge.
- `noor::render`: Vulkan 1.3 instance/device/swapchain, 2 frames in flight, sync2 barriers,
  clear-to-color frame loop, swapchain rebuild on resize/out-of-date, validation message capture.
- `noor_diag` diagnostics tool: reports GPU, clears to Noor purple (sRGB-correct), FPS in title.
- Tests: 18 unit tests (log, error, assert, GUID) and a GPU smoke test (180 frames, validation on,
  fails on any validation error). Debug and RelWithDebInfo both pass on the dev machine.

**Fixed during development**
- Vulkan loader errors from third-party implicit layers (ReShade on the dev machine) were counted
  as validation errors. Now reported as `[loader]` warnings; real validation errors still fail.
- Startup no longer builds the swapchain twice (resize event with unchanged size is ignored).

**Known limitations**
- Window resize and maximize tested by hand by Tom (1280×720 → 222×161 → 1920×1017; swapchain
  rebuilt correctly, 1,311 frames, zero validation errors). Minimize/restore and monitor changes
  remain untested. No automated resize test yet.
- No live redraw while dragging a window edge: Win32's modal resize loop pauses our frame loop,
  so the swapchain rebuilds only when the drag ends and Windows stretches the last frame meanwhile.
  Fix before the editor (render from an SDL event watch during resize).
- No negative test yet proving a deliberate validation error fails the smoke test.
- Windows only. No CI yet (GitHub Actions runners have no GPU; unit tests can run there).
