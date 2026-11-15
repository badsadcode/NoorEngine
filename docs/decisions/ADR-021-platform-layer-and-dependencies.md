# ADR-021: Platform layer and dependency policy

**Status:** Accepted
**Date:** 2026-11-15

## Context
Windows, input, monitors, high-DPI and Vulkan surface creation are a lot of fiddly OS code.
The spec says "own the architecture" but not "rewrite OS glue".

## Options
- **A: SDL3 (zlib).** Mature, cross-platform window/input/gamepad/audio-device layer, Vulkan
  surface helpers, used by many shipped games.
- **B: GLFW (zlib).** Smaller, but no gamepad mapping database, audio or as rich input.
- **C: Raw Win32.** Full control, but Linux becomes a second rewrite.

## Decision
**SDL3**, wrapped behind `noor::platform`. No SDL types appear in engine headers outside
`engine/platform/src`, except the Vulkan surface bridge in `render/`.

Dependency policy:
1. Every dependency is listed in `third_party/manifest.json` with source, exact version,
   commit, license and why we use it.
2. Pin exact commits; never track a branch.
3. Prefer releases at least two weeks old.
4. License must satisfy ADR-019.

## Consequences
SDL3 is built from source (static). First configure takes a couple of minutes.

## Reviewed by
Sage.
