# ADR-036: Skeletal animation

**Status:** Accepted, with one change from the proposal (Milestone 8C, part 1, 2026-11-20): **bones are entities** (below)

## Context

NoorEngine's models are rigid. The glTF importer reads meshes, materials and collision nodes, but not skins or
animations, and vertices carry no joint indices or weights. Characters are capsules or sprites. Milestone 8C brings
skeletal animation: skinned meshes from glTF (any rig Blender exports, Rigify included), clips, blending, a simple
state machine, root motion, an editor preview and retargeting between humanoid rigs, with Alden Wayfarer as the test
character ([plan](../milestones/M8_PLAN.md#8c-skeletal-animation), spec 18.1-18.3).

The engine's habits set the constraints: deterministic runtime (fixed steps, built games replaying bit for bit,
golden images), scripts in Lua and C# with one API, instanced drawing (ADR-032), and plain glTF from Blender as the
main asset path.

## Options

1. **Bones as scene entities** (each joint an entity, animation writes their transforms): simple to inspect and to
   attach things to, but thousands of entities rewritten every frame for a crowd, and saved scenes full of poses.
2. **Bones in a runtime pose** (an array per character, not in the scene), with sockets for attachments: fast,
   clean saves, needs its own inspection tools.
3. **Sampling on the GPU** (clips in buffers, compute): fastest for huge crowds, hardest to keep deterministic and
   to read back for gameplay (root motion, sockets, events).
4. **Sampling on the CPU, skinning on the GPU**: poses where gameplay can read them, skinning where it's cheap.
5. **Retargeting** by copying local rotations (breaks with different rest poses) or relative to each rig's rest pose
   through a shared humanoid map (Unity's "humanoid", Unreal's IK retargeter in spirit).

## Decision

- **Bones are entities (option 1, changed in part 1), sampling on the CPU and skinning on the GPU (option 4).** The
  proposal chose a hidden runtime pose (option 2). Part 1 found that placing a model has always made an entity for
  every glTF node, joints included, and that rigid characters (the CC0 robot) are pieces parented to those joint
  entities. Keeping that: a skinned mesh's entity lists its skin's joints by entity id, and the joints' world
  matrices make its palette. Sockets come for free (parent a prop to a hand's entity), scripts read and move bones
  as entities, the Inspector shows them, and rigid characters animate the same way. The cost the proposal feared is
  measured instead: 100 characters of 17 joints (1,900 entities) take about 1.2 ms of CPU to gather a frame, 0.2-0.4
  ms of GPU to draw. Part 2 writes clips into joint transforms and measures again; a hidden pose stays the fallback
  for crowds if that's too slow.
- **Skinning in the vertex shader** with linear blend skinning (4 influences). No new pipelines or descriptors: each
  skinned mesh's skin data (24 bytes a vertex: four 16-bit joints, four float weights) and each draw's palette (3×4
  rows per joint, in a per-frame buffer) are read through buffer device addresses passed in push constants (Vulkan
  1.2 core; required, with 64-bit shader integers). A skinned draw's object matrix is identity, its bounds come from
  its posed joints, and it's its own batch; shadows use the same path. The CPU reference (`asset/skinning.h`) is the
  same sum, used by tests and by the editor's picking and framing.
- **An `animator` component** plays a clip or runs a controller asset (`.nooranim`): parameters, states (a clip or a
  1D/2D blend space), transitions (conditions, cross-fade time), layers with bone masks, additive layers. Scripts
  set parameters, play and cross-fade, and hear `on_animation_event(name)` and the end of clips (8B's pattern).
- **Root motion** is opt-in per clip and feeds the character controller when there is one.
- **Retargeting** through a humanoid bone map (about 20 core bones, fingers optional) with presets for Rigify
  (`DEF-`), Mixamo (`mixamorig:`), the Unreal mannequin and Blender's metarig, auto-detected and overridable; the
  transfer works relative to each rig's rest pose, with the hips' height scaled by leg length.
- **glTF only** until FBX through ufbx (MIT) has been tried on real files (part 7, research first).
- **Deterministic maths:** quaternion blending with a fixed order and the engine's own functions (as 8B did for sine
  and cosine), so poses are the same in Debug and Release and in built games.

## Consequences

- Characters become a renderer feature (palettes per frame, read by address) and a runtime system (sampling,
  blending, root motion, events), sharing the scene, scripts and tests the rest of the engine has.
- Bones are ordinary entities: saved with the scene in their rest pose, selectable, scriptable. Animation writes
  their transforms at runtime (not into the saved scene). *Part 2:* the model's root lists the entity each of its
  file's nodes became (`"model": {"nodes"}`), so clips (which name nodes by index) find them; the animator samples at
  the moment the frame ends (after scripts), and writes every bone any of the model's clips moves.
- *Part 2:* sprites and skeletal animators share one script API (`play_animation`, `stop_animation`,
  `set_animation_speed`, `on_animation_end`) and the same component fields; 100 walking characters cost 0.43 ms of
  game update (sampling and writing 800 bones).
- *Part 3:* blending is normalised lerp (exact at the midpoint, no trigonometry), so it's bit-stable as decided; the
  part-2 sampler's slerp (one clip's own keys) uses the standard library's `acos` and `sin`, which the built-game
  checks have so far found identical; if a platform ever differs, it moves to the same nlerp. Controllers carry state
  between frames (states, phases, fades), stepped in `update` so a state's end reaches scripts that frame; replays are
  identical because steps are. Blend spaces play their clips at one shared phase (feet in step). The script ABI went
  to v5 (cross-fades, controller parameters). 100 characters on a three-layer controller: 1.0 ms of game update.
- *Part 4:* root motion is translation only (yaw stays with scripts), horizontal (the vertical bob stays in the pose),
  measured per frame as the root bone's movement over that frame's clock (a looping clip adds a whole cycle each time
  it wraps, so nothing drifts) and given to the nearest character controller at or above the model as velocity, or
  to the model's position. Events are named moments on clips (the animator) or controller states. Bones-as-entities
  made sockets free: a prop is a child of a bone; `bone(name)` finds one. Script ABI v6 (bones, events).
- Crowds cost CPU per bone (gathering the scene) and per character (sampling); the crowd benchmark in part 8 says
  where the limit is. GPU sampling and a hidden pose stay later options.
- The device must support buffer device addresses and 64-bit shader integers (every desktop GPU we target does); one
  that doesn't is refused at start with that reason.
- Retargeting quality depends on correct rest poses and maps; the presets and warnings must say plainly when a rig
  doesn't match.

## Unresolved

- ~~Test characters~~: settled (CC0 RobotExpressive and SimpleSkin; our own generated Ward).
- Whether FBX support happens in 8C (part 7's research decides).
- IK, look-at, foot placement and ragdolls (spec 18.2) are after 8C.
