# Changelog

Technical, honest, newest first. "Works" means tested; anything else says what it is.

## [Unreleased] — Milestone 0 in progress

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
- Window resize, minimize/restore and monitor changes are implemented but not yet tested,
  neither by hand nor by automated tests.
- No negative test yet proving a deliberate validation error fails the smoke test.
- Windows only. No CI yet (GitHub Actions runners have no GPU; unit tests can run there).
