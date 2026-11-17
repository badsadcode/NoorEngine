# Milestone 4 gate review: core scripts and everyday gameplay

**Date:** 2026-11-17 · **Reviewer:** Sage · **Verdict:** ✅ **Complete, with documented exceptions**

Spec reference: master spec section 29, Milestone 4 ("Goal: make actual interactive projects,
not just view static meshes"). Plan: [M4_PLAN.md](M4_PLAN.md).

## Deliverables

| Deliverable (spec) | Status | Evidence |
|---|---|---|
| C# and Lua runtime lifecycle and bindings | ✅ | ADR-027 `noor_rt_api` (v3); Luau (ADR-009) and C# (ADR-008: in-process in the editor with collectible reload, hostfxr in the player). The same hooks and the same API in both; NoorFPS-lite is written twice, line for line |
| Input events | ✅ | Actions and axes from `Settings/input.json`, pressed/down/released, mouse look with capture (Esc releases, focus loss releases) |
| Physics basics | ✅ | Jolt (ADR-010): rigid bodies, box/sphere/capsule/mesh colliders, triggers, collision and trigger hooks, raycasts, impulses, render interpolation |
| Camera and character controllers | ✅ | `camera` component; `character` (Jolt CharacterVirtual): slopes, steps, walls, jumping |
| Initial in-editor code editing | ✅ | NoorCode MVP (part 7b): tabs, Lua/C# highlighting, find, save, errors marked on their line and linked from the Console |
| Build/run scripts | ✅ | C# compiled on Play and by `noor_cli build`, compile errors as `Scripts/X.cs:7: CS1002: …`; Lua hot-reloads while playing |
| Debug logs | ✅ | Script logs with file and line in the Console; script errors stop only that script; `<game>.exe.log` next to built games |
| Gameplay UI | ✅ | HUD (part 7a): text and rectangles, anchors, UI units, distance-field font with Polish |
| Audio playback | ✅ | miniaudio (ADR-011): clips, one-shots, 3D sources, listener, buses, streaming; a lost device (earbuds in their case) is rebuilt |

## Gate criteria

| Gate (spec) | Result | Evidence |
|---|---|---|
| A developer writes a script from NoorCode | ✅ | By hand (the project owner, 2026-11-17): the player script opened and worked in NoorCode. Smoke test: open at a line, type, Ctrl+S, compile error marked on line 7 |
| …runs a character | ✅ | By hand: walking, sprinting, jumping, looking (the hand tests found the jitter and frame-pacing problems that got fixed). Unit tests: slopes, steps, walls, jumps |
| …plays a sound | ✅ | By hand: music and one-shots on Bluetooth earbuds, and the sound came back after the earbuds went into their case. Audio unit tests mix to memory |
| …saves a scene | ✅ | Editor smoke test saves and reads the file back; a hand-made Sound Demo project |
| …and exports a working build | ✅ | File ▸ Build Game / `noor_cli build`; the built Sound Demo ran by hand from a desktop shortcut. ctest runs built games (Lua and C#) |
| A basic FPS sample can be created using public APIs | ✅ | **NoorFPS-lite** (`samples/NoorFPS-lite`): only the documented script API, in C# and as a Lua twin. ctest builds it with `noor_cli` and runs both versions' demo for 15 s with no input: 24 hits each, zero errors |

## What the sample found (fixed in part 8)

Writing a real game against the public API is what this gate is for, and it earned its keep:

1. **C# couldn't set an entity's properties**: `Entity.Position = v` didn't compile (CS1612)
   because `Entity` was a struct handed out by a property. It is now a class (a handle, equal by
   id). `Noor.Find`, `Parent`, `Child` still return `Entity?`; code that used `.Value` on them
   must drop it.
2. **A ray from the player's eyes hit the player**: `raycast(…, ignore = player)` didn't skip a
   character's controller body. Fixed, with a unit test.
3. **Tests could be disturbed by a click**: a built game under test reacted to a real click on
   its window. `noor_player --no-input` now gives the game no keyboard or mouse; the player and
   sample tests use it.

## Exceptions (accepted, tracked)

1. **NoorCode is an MVP**: no code completion, no live error checking, no debugger (spec 7 is the
   full IDE; it needs Roslyn and a Luau analyser). Open tabs aren't remembered.
2. **Script API gaps**: no spawning of prefabs at runtime, no timers or coroutines, no `require`
   between Lua files, no messages between scripts (NoorFPS-lite talks through a component). No
   gamepad input.
3. **Builds need runtimes installed**: the .NET 10 runtime for C# games and the VC++ runtime;
   no app-local or self-contained option yet. The editor's File ▸ Build Game packs the **Debug**
   player of the build it came from (slower; noor_cli warns).
4. **Raycasts and triggers**: whether rays should pass through trigger volumes (a shot through an
   invisible zone) is not decided or tested yet.
5. **HUD**: no images, no other fonts, no buttons, no text wrapping.
6. **Tests on a real desktop**: GPU tests open real windows; a key or click by whoever uses the
   PC can reach them (an Esc closed `lifecycle.window` once during this review; a rerun passed).
   Built games under test now ignore input (`--no-input`); noor_diag's window tests don't yet.
7. **Carried:** textures and PBR materials (Milestone 5), LOD loading,
   GPU device-loss recovery, prefab child edits, the automated HiDPI run (see the M3 review).

## Measurements
- Tests: **154/154** (Debug, incl. GPU, editor and the sample), including 133 editor smoke checks
  (real posted input to the viewport, play at ≥ 58 updates/s, zero Vulkan validation errors).
- NoorFPS-lite demo, 15 s of game time at a fixed 1/60 s: 24 hits (C#), 24 hits (Lua).
- C# compile on Play (dotnet build, smoke test project): 1.2 s.
- Editor Play: ~58–60 game updates per second on a 60 Hz display (was ~34 before part 6's fix).

## Milestone tradition: Alden discovers something
Proposed line for the Milestone 4 picture (Tom and Noor make it):
*"Alden discovers that the world now answers back: things fall, scripts think, and he can walk."*

## Carried into Milestone 5 (PBR lighting, HDRI and materials)
- Textures and material assets first: the arena walls, and The Uncounted, Chapter Two, both need
  them (IDEAS.md).
- Decide raycasts vs triggers; prefab spawning and timers when the first game needs them.
