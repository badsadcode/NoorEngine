# ADR-036: Skeletal animation

**Status:** Proposed (Milestone 8C plan, 2026-11-20)

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

## Decision (proposed)

- **Options 2 and 4.** Bones live in a runtime pose per character; props attach through sockets (an entity that
  follows a named bone); scripts can read bone transforms. Clips are sampled and blended on the CPU from game time,
  deterministically; the vertex shader skins with linear blend skinning (4 influences) from a per-frame storage
  buffer of joint palettes, with a palette offset per draw so instancing keeps working; shadows use the same path.
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

- Characters become a renderer feature (a skinned pipeline variant, palettes per frame) and a runtime system
  (sampling, blending, root motion, events), sharing the scene, scripts and tests the rest of the engine has.
- Bones aren't editable as entities: the Animation panel must show the skeleton, and sockets cover attachments.
- Crowds cost CPU sampling per character; a crowd benchmark in the sample says where the limit is, and GPU sampling
  stays a later option.
- Retargeting quality depends on correct rest poses and maps; the presets and warnings must say plainly when a rig
  doesn't match.

## Unresolved

- Test characters that may be published (Mixamo's can't): Tom's Blender characters, CC0 sets, Khronos samples.
- Whether FBX support happens in 8C (part 7's research decides).
- IK, look-at, foot placement and ragdolls (spec 18.2) are after 8C.
