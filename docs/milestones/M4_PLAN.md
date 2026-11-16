# Milestone 4 plan: core scripts and everyday gameplay

**Goal (spec):** make actual interactive projects, not just view static meshes.

**Gate (spec):** a developer writes a script from NoorCode, runs a character, plays a sound,
saves a scene and exports a working build; a compatible basic FPS or 2D sample can be created
using public APIs.

## Decisions
ADR-027 (runtime, loop, scripting API), ADR-008 (.NET hosting), ADR-009 (Lua, proposal),
ADR-010 (physics: Jolt), ADR-011 (audio: miniaudio).

## Parts (each one ends tested, documented and pushed)

| Part | What | Proves |
|---|---|---|
| 1 | `runtime` module: `World`, frame order, fixed step, input actions/axes (`Settings/input.json`), built-ins moved over; play mode runs on it; game input from the viewport | The loop and input work headlessly and in the editor |
| 2 | Lua scripting (ADR-009) + the `noor_rt_*` API (entities, transforms, input, time, log) | A script moves something in play mode |
| 3 | Physics (Jolt): rigid bodies, colliders (incl. static meshes from GLBs), raycasts, triggers, `character` controller, camera component | A capsule walks and jumps around a BadBuilding street |
| 4 | C# scripting (ADR-008): `NoorEngine.Scripting`, script project generation and build, reload on Stop, API parity with Lua | The same behaviour in C# and Lua |
| 5 | Audio (miniaudio): audio assets, sources, listener, buses, 3D; script API | Footsteps and a one-shot sound in 3D. **Done** (one-shots and 3D tested; footsteps come with the sample in part 8) |
| 6 | `noor_player` + `noor_cli build`: export a runnable build (scene, assets, scripts, runtime); CoreCLR hosting in the player | A build runs outside the editor. **Done** (6a: player, build, File ▸ Build Game; 6b: C# via hostfxr) |
| 7 | NoorCode MVP (code editor panel: open/edit/save, highlighting, compile errors), gameplay UI (HUD text and shapes), mouse capture for FPS look | Scripts written and fixed inside NoorEditor. **7a done:** HUD (mouse capture was done in part 3b). **7b done:** NoorCode MVP (completion and debugging are later) |
| 8 | Sample: **NoorFPS-lite** (walk, sprint, jump, look, shoot targets, sounds, HUD) in C# and a Lua twin of its basic behaviours; Milestone 4 review | The gate |

## Highest risks (settle early)
1. **Input inside an embedded viewport**: keyboard focus and mouse capture for FPS look in a
   native child window inside Avalonia (part 1 starts it; part 7 finishes mouse capture).
2. **CoreCLR in two hosts** (editor in-process with collectible reload, player via hostfxr) with
   one scripting library (part 4/6).
3. **Character controller feel** (Jolt `CharacterVirtual` tuning: slopes, steps, BadBuilding stairs
   and kerbs) (part 3).
4. **Build packaging**: what a build contains, and the .NET runtime requirement (part 6).
5. **Scope**: the FPS sample can swallow the milestone; it uses only public APIs and stays "lite".

## Milestone tradition: Alden discovers something
Every major milestone, Alden Wayfarer (the mascot, devlog #015) discovers what the engine just
learned. For Milestone 4 the review proposes a line like *"Alden discovers that the world now
answers back: things fall, scripts think, and he can walk."*; Tom and Noor make the picture.
