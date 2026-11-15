# ADR-027: Runtime module, game loop and the scripting API

**Status:** Accepted · **Date:** 2026-11-15 · **Milestone:** 4

## Context
Milestone 4 makes scenes interactive: scripts in C# and Lua, input, physics, audio, a character,
and a standalone build. Play mode (ADR-026) runs built-in behaviours on a copy of the scene from a
UI timer. Before adding languages and physics we need one runtime that the editor's play mode and
a standalone player both use, a defined frame order (spec 9.4), and one scripting API that C# and
Lua both bind (spec 24.4 "scripting parity").

## Options
- **A. A native `runtime` module (`runtime::World`) hosted by both the editor and a player exe.**
  Systems (input, scripts, physics, audio, behaviours) run in a fixed order; scripts reach the
  engine through one C API.
- **B. Run gameplay in C# only (the editor is already .NET) and add Lua later.** Fast to start, but
  the player would still need native hosting, Lua would bind a different surface, and engine rules
  would live in two languages.
- **C. A separate player process even inside the editor.** Best isolation; costs IPC for every
  viewport frame and debugging hop. Possible later (ADR-026 notes it).

## Decision
**A.**
- New module `engine/runtime` (depends on foundation, asset, scene; physics/audio/scripting are
  systems plugged into it, so the core stays free of them): `World` owns the running scene, time,
  input and the systems.
- **Frame order** (spec 9.4): input → `start` for new entities → `update(dt)` → fixed steps
  (`fixed_update` at 60 Hz, physics after scripts, at most 5 steps per frame) → `late_update` →
  destroy queued entities → publish render state. Variable `dt` is capped at 0.1 s.
- **Input** is host-neutral: hosts feed an `InputState` (keys, mouse, wheel) each frame; projects
  map keys to **actions and axes** in `Settings/input.json` (defaults provided). Scripts ask for
  actions ("jump", "move_x"), not keys.
- **One scripting API, two languages.** The runtime exposes a C API (`noor_rt_*`: entities,
  transforms, components, input actions, time, log; later physics queries, audio). C# P/Invokes
  it; Lua binds the same functions. A feature exists for scripts once it exists in that C API.
- **Hosts:** the editor's play mode (the document owns a `World`; ADR-026's contract is kept), and
  `noor_player` (C++ exe: SDL3 window + renderer + `World`, loading an exported build).
- Built-in behaviours (`rotator`, `input_mover`) stay as runtime systems for quick prototypes and
  tests.

## Consequences
- Physics (ADR-010), audio (ADR-011), C# (ADR-008) and Lua (ADR-009) arrive as systems with their
  own tests, without changing the loop.
- The editor still drives time from a UI timer in Milestone 4; the player runs its own loop. Both
  call the same `World::frame`.
- The `noor_rt_*` C API is pre-1.0 like the editor API; ADR-007 (plugin ABI) decides stability.

## Reviewed by
Sage.
