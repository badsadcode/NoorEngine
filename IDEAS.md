# The Idea Parking Lot 🅿️

Tom has thousands of ideas every five seconds. Noor is a nerdy goblin with a sketchbook.
This file makes sure **no idea is lost** and **no idea derails the current milestone**.

## How it works

1. Anyone adds an idea to **Inbox** below: one line, who, date. No justification needed.
2. Between milestones, Sage triages the inbox into a bucket and notes the dependency chain:
   - **Foundation**: needed by many systems, build early
   - **First-class**: core product capability, full vertical slice when reached
   - **Expansion**: legit long-term feature, no promise yet
   - **Research**: needs a spike, benchmark or license check first
   - **Bonfire** 🔥: lovingly rejected, with the reason
3. Ideas only move into a milestone through an ADR or the roadmap, never mid-sprint.

## Inbox

| Idea | From | Date |
|---|---|---|
| **An animation studio inside the engine**: NoorEngine's own animation rig that reads imported animations (Rigify and other rigs) and helps edit them: pose, key, adjust and fix animations in the editor rather than only in Blender. "That would be awesome." **Sage's notes:** builds on 8C (skeletons, playback, the humanoid bone map and retargeting); a natural follow-up group after 8C, with an IK solver, a timeline and curve editing as its core. | Tom | 2026-11-18 |
| **Clouds that do more work**: after choosing the environment presets, "the clouds could do some more work, but we can leave that for later". **Sage's notes:** today's clouds are thin layers lit by an approximation (Milestone 8A, part 4): no depth, no shapes you could fly round, no self-shadowing between layers. The fix is volumetric clouds (NoorSky phase C, already planned after 8A): ray-marched cloud volumes with real shapes, light and shadow. Meanwhile, cheaper steps: cloud types beyond one noise (cumulus towers, cirrus streaks), and per-layer tint and brightness controls in the environment. **Update 2026-11-19 (Tom and Noor):** "beautiful, detailed, but they still look painted on a flat surface". **Scheduled as 8A part 7b** ([ADR-034](docs/decisions/ADR-034-volumetric-cloud-layers.md)): volumetric layers beside the flat ones; the flat layers stay exactly as they are (wanted for a classic layered-sky look). | Tom | 2026-11-19 |
| **FBX import** ("if we can use FBX, that's awesome; if not, oh well"). **Sage's notes:** Autodesk's FBX SDK is proprietary (closed binaries, its own licence terms); **ufbx** (MIT, a single-file C library that reads FBX, used by Godot) avoids that. Research item for 8C: licence check, robustness on Blender/Mixamo/Unreal FBX files, and converting to the same skeletons and clips as glTF. glTF stays the main path. | Tom | 2026-11-18 |

## Triaged

*First triage: 2026-11-18, after Milestone 7 (Sage). Rows keep their original text; the line under each
table says where it goes and what it waits on. Moving into a milestone still needs the roadmap or an ADR.*

### Foundation
### First-class

| Idea | From | Date |
|---|---|---|
| **NoorSky: environment presets and authoring** ("beautiful defaults for beginners, full creative freedom for everyone else"). A `.noorenv` asset that sets sky, sun/moon/stars, clouds, fog and aerial perspective, horizon silhouettes, lighting and exposure together; a gallery of 12 presets (Clear Noon, Golden Evening, Overcast, Foggy Dawn, Stormy Coast, Moonlit Night, Fantasy Crisp, Cold Morning, Autumn Afternoon, Rainy Grey, Desert Haze, Alien World) to apply in one click, duplicate and edit; build your own from scratch; time of day and weather blending later; graph/Lua drivers later. Phases A (six presets, procedural sky, simple layered clouds, editor gallery) → B (horizon layers, blending, more presets) → C (volumetrics, weather, alien skies). Brief + 12 mood references (concept art, not skyboxes): `NoorSky_Sage_Package.zip`, kept outside the repo for now. **Sage's notes:** builds on what exists (the environment entity with HDRI or procedural sky → prefiltered cubemap + SH irradiance, the sun light with cascaded shadows, exposure presets). Missing pieces: fog/aerial perspective (none today), a real atmosphere model (sun-driven, so time of day works), clouds, a night sky, an environment asset rather than per-scene settings. Biggest risk: the "environment rebuild" is CPU-side and cached today (fine for presets, too slow for a moving sun), so time of day needs GPU prefiltering or blending between baked states. Fits well as a milestone after M7 (Hillside + forest would be the showcase scene the brief asks for). | Tom & Noor | 2026-11-18 |
| **The first real game on NoorEngine: The Uncounted, Chapter Two** (sequel to github.com/badsadcode/TheUncounted, Tom's data-driven first-person action RPG on raylib/C++20: JSON content, dialogue, quests, combat, save/load, mods, a level editor). Uncle Wispr will be in it: an old, almost deaf uncle with a bronze ear trumpet who mishears everything. "No rush." (Textured walls will be needed.) Before planning: read its HANDOFF.md and docs/ARCHITECTURE.md to see which systems (dialogue, quests, inventory, combat, saves, content validation) the engine needs and what can be carried over as data. | Tom | 2026-11-17 |
| **2D sprites and directional billboards**: sprite-sheet assets (grid, frames, named animations, 8 viewing directions), sprite rendering in 2D and as camera-facing billboards in 3D (Doom-style enemies), animation playback from scripts. Test data offered: a character sheet from The Uncounted (8 directions × idle, walk, run, attack, hurt, plus death poses). | Tom | 2026-11-17 |

- **NoorSky** → **scheduled**: Milestone 8, group 8A ([M8_PLAN.md](docs/milestones/M8_PLAN.md), brief in
  [docs/ideas/NOORSKY.md](docs/ideas/NOORSKY.md)); underwater and space environments added (Tom, 2026-11-18).
  Originally triaged as: the roadmap's Milestone 8 "sky/weather" group. Waits on: fog and aerial perspective in the
  renderer, a sun-driven atmosphere, and environment lighting rebuilt on the GPU (today it's baked on the
  CPU, too slow for a moving sun). Hillside with its forest is the showcase scene. Phase A first.
- **The Uncounted, Chapter Two** → the first real game, so it should steer the order of what comes next
  (read 2026-11-18: its HANDOFF.md). Chapter Zero draws creatures and NPCs as **8-direction pre-rendered
  billboard sprites**, blends **zones of atmosphere and weather** (fog, rain, dust, music crossfades), and
  uses particles and emitters; its gameplay (dialogue, quests, inventory, combat, saves) is data plus code
  that a game on NoorEngine would carry over as data and scripts. It needs sprites, atmosphere/weather and
  particles before it needs skeletal animation. **Confirmed 2026-11-18: Chapter Two keeps sprite characters.** Waits on: the items above and below, then a
  gameplay-framework pass (dialogue/quest/save as engine-side helpers or as a game template: to decide).
- **2D sprites and directional billboards** → needed by The Uncounted, so first-class. Builds on instancing
  (ADR-032 batches thousands of billboards) and the texture pipeline (atlases). Waits on: a sprite-sheet
  asset type and an animation-playback API for scripts. Small enough to be one part of a milestone.

### Expansion

| Idea | From | Date |
|---|---|---|
| **Fluid "parkour" FPS movement** (Titanfall 2 feel): crouch-slide, wall-running, wall-jumping, momentum, camera tilt and FOV kick. Possible as a script on today's API for the camera side; the movement needs character-controller additions: full 3D velocity control (not only horizontal), gravity scale per character, crouch (capsule resize with a ceiling check), ground normal/info, shape casts (sphere/capsule) for wall and ledge detection. Then lots of feel tuning by hand. Could become a NoorFPS-lite "movement lab" scene. | Tom | 2026-11-17 |
| **Alden Wayfarer is NoorEngine's mascot**, the "first citizen" and first explorer of Bad Ideas Works worlds, carried over from Thornwake where it all started. Possible uses: hero of the sample games (NoorFPS-lite, third-person sample), default name of Create ▸ Player, devlog/website art, splash screen. Concept sheet exists (blocky low-poly explorer: teal tunic, gold buckle, backpack with bedroll, walking staff; poses exploring/resting/map check/discovering/victory/looking; a mossy statue "He went first because somebody had to"; tablet "Explore · Test · Break · Repeat"). Next: a GLB of him (NoorMD or hand-made) for samples and the editor. Public: introduced in devlog #015 (approved by Tom). | Tom & Noor | 2026-11-15 |

- **Parkour movement** → expansion; the character controller additions it lists (3D velocity, gravity scale,
  crouch with a ceiling check, ground info, shape casts) are useful to every game and could land with a
  gameplay pass; the feel tuning is a sample's job ("movement lab").
- **Alden Wayfarer** → expansion (the identity is settled; the work is an asset). Waits on: a GLB of him
  (NoorMD or by hand); his poses want skeletal animation (Milestone 8's animation group) or a pose library.

### Research
### Bonfire 🔥
