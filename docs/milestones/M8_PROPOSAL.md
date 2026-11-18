# Milestone 8: a proposal (not a plan yet)

**Status:** answered 2026-11-18 → [M8_PLAN.md](M8_PLAN.md) · **Date:** 2026-11-18 · **From:** Sage, after the
[Milestone 7 review](M7_REVIEW.md) and the first [ideas triage](../../IDEAS.md#triaged).

## What the roadmap says

Milestone 8 is "Animation, FX, water, weather and richer worlds": skeletal animation, improved
particle editing, sky/weather, water bodies, volumetrics and streaming, "in technically sensible
order", and it "can be separate milestone groups". Gate: each has editor authoring, runtime
behaviour, serialization, docs, performance notes and tests.

That's six large systems. Done as one milestone, each would be thin. **I propose splitting it into
groups that each end in their own review**, like Milestones 5–7 did with parts.

One honest correction to the roadmap's wording: "*improved* particle editing" assumes particles exist.
NoorEngine has none yet. The Uncounted has its own, on raylib.

## What should decide the order

1. **The first real game.** The Uncounted, Chapter Two is the first game planned on NoorEngine. Its
   Chapter Zero (read 2026-11-18) draws creatures and NPCs as **8-direction billboard sprites**,
   blends **zones of atmosphere and weather** (fog, rain, dust, with music crossfades), and uses
   **particles and emitters** everywhere. It has no skeletal characters.
2. **NoorSky** (the package from Tom and Noor): presets that make a new scene look good at once,
   and a sky you can author. It needs fog and a sun-driven atmosphere, which every outdoor scene
   (and Hillside's forest) is missing today.
3. **What unlocks the most** (spec 29.2): fog and atmosphere affect every outdoor picture; particles are
   used by weather (rain, dust), FX and gameplay; billboards reuse Milestone 7's instancing directly.

## Proposed groups, in this order

| Group | What | Why here |
|---|---|---|
| **8A: Sky, atmosphere and weather** (NoorSky phase A) | A sun-driven sky and aerial perspective, fog (distance and height), environment lighting rebuilt on the GPU (so the sun can move), a simple layered cloud sky, a night sky (moon, stars), a `.noorenv` asset with six presets and a gallery in the editor, then **weather zones** that blend fog/sky/light across space (The Uncounted's pattern) | Every outdoor scene benefits at once; it's what NoorSky asks for first; zones are what The Uncounted needs |
| **8B: Particles and sprites** | A particle system (emitters, a GPU-friendly simulation, an editor panel with live preview, from scripts too) used for rain, snow and dust in 8A's weather; **sprite sheets and directional billboards** (The Uncounted's creatures; batched by ADR-032) with animation playback from scripts | The Uncounted needs both; weather needs particles to rain |
| **8C: Skeletal animation** | Skinned meshes from glTF, an animation player, blending and a simple state machine, root motion, an editor preview; Alden Wayfarer as the test character (a GLB needed) | Big and self-contained; needed for 3D characters and the mascot, not for The Uncounted's style |
| **8D: Water** | Water bodies (lakes, a sea plane) with reflections, shorelines that read the terrain, underwater fog (reusing 8A) | Builds on 8A's fog and sky |
| **Later in 8 (or 9)** | Volumetric fog and clouds (NoorSky phase C), streaming for big worlds | Volumetrics need 8A first; streaming belongs with Milestone 9's big worlds |

## Answers (2026-11-18)

1. **Order:** 8A → 8B → 8C → 8D, as proposed.
2. Not answered yet (sprites or 3D for The Uncounted, Chapter Two); it doesn't change 8A.
3. **The six presets:** yes, plus environments for **underwater** and **space** (space: 8A part 5;
   underwater: with 8D's water).
4. **The NoorSky brief:** yes, in the repository (`docs/ideas/NOORSKY.md`; the images stay out).
5. **NoorMD:** it needs a rework first; it keeps its Unreal, Godot and general exports and gets a
   NoorEngine mode later. For now 3D assets come from **Blender**; trees, grass and rocks from NoorMD now
   and then. And animation should support popular rigs, Rigify included (8C: any glTF rig plays;
   retargeting through a humanoid bone map with Rigify, Mixamo and Unreal-mannequin presets).

## Questions only Tom (and Noor) can answer

1. **The order.** Is 8A → 8B → 8C → 8D right, or should something come earlier (animation, say, if a
   3D character game is planned sooner)?
2. **The Uncounted, Chapter Two: sprites or 3D characters?** If it keeps Chapter Zero's pre-rendered
   sprites, 8B serves it directly and 8C can wait. If its characters become 3D, 8C moves up.
3. **NoorSky phase A's six presets** (Clear Noon, Golden Evening, Overcast, Foggy Dawn, Moonlit Night,
   Stormy Coast): still the right first six?
4. **May the NoorSky brief go into the repository** (as `docs/ideas/NOORSKY.md`)? It names Tom and Noor
   and the repository is public. The 12 reference images are concept art (11 MB); I'd keep them out
   unless you want them in.
5. **NoorMD:** when its NoorEngine export mode exists, rocks can join (Milestone 7's open exception),
   and the export guide's suggestions (mesh quantization, the pivot, `knees`) would make forests lighter
   and tidier. Is that on NoorMD's list, and is a headless NoorForm wanted (graphs that grow trees)?

## Carried over from Milestone 7 (to schedule somewhere)

Level-of-detail cross-fades (and a fade at grass's draw distance), a procgraph cache size limit, graph
editor zoom/subgraphs/comments, moving a single scattered tree, trees following unsaved sculpting.
(Rocks and convex collision were done after the review.)

When the questions are answered, this becomes `M8_PLAN.md` with parts, proofs and risks, like
[M7_PLAN.md](M7_PLAN.md).
