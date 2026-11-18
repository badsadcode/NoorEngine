# ADR-032: Instanced drawing and levels of detail

**Status:** Proposed (implemented and measured in Milestone 7, part 6; to accept at the Milestone 7 review) · **Date:** 2026-11-18 · **Milestone:** 7

## Context
Milestone 7 places forests: thousands of copies of a few NoorMD trees, each with four levels of
detail (Noor Interchange v0.4 `lods`, with the distance each level is for). Until now every object
part was one draw call with its matrix in push constants, every caster was drawn four times
(once per shadow cascade), nothing was culled against the camera, and models were always drawn at
full detail. A 2,000-oak test forest ran at **5 FPS**: 192 ms a frame, 189 ms of it on the CPU
collecting the scene (a quadratic parent lookup), 22–35 ms on the GPU (30.5 million triangles,
4,784 draw calls). The plan's gate: 2,000 trees with four levels at 60 FPS in a Release build.

## Options
- **Instancing**
  - **A: The renderer batches on its own.** Callers keep handing it one `DrawItem` per object; each
    frame it culls them and groups the ones that share a mesh range, material and pipeline into
    one instanced draw, with matrices in a per-frame instance buffer. Every caller (editor viewport,
    player, material preview) benefits; no API change.
  - **B: An explicit instance API** (`InstanceSet` handles, persistent GPU buffers per set). Less CPU
    per frame for huge static sets, but every caller must change, and scattering (part 7) would be
    the only user at first.
  - **C: GPU-driven** (compute culling, indirect draws). The fastest and the most complex; not
    needed at today's scale.
- **Levels of detail**
  - **A: Per placed model, by camera distance**, using the manifest's `max_distance`s (scaled for
    the camera's field of view and the model's size). The whole model switches: level 0 is its own
    entities, as authored; a lower level is drawn from its `.lod<n>.glb` file, placed by the root.
  - **B: Per mesh/node.** Levels don't share nodes (NoorForm's billboard level has one mesh where
    level 0 has a trunk and leaves), so mapping parts across levels doesn't work in general.
  - **C: Screen-size based** (projected bounds). Better in theory; the assets already carry
    distances tuned by their tool, so we use those.

## Decision (proposed)
**Instancing A and LOD A.**
- **Instances:** a 64-byte record per instance (the matrix's top three rows, the tint) in a
  host-visible buffer per frame in flight, grown as needed, bound as vertex binding 1 (per instance).
  Push constants shrink to what a batch shares (material ids, splat tiling: 32 bytes).
- **Planning** (`vulkan_instancing.cpp`, before recording): camera frustum culling (bounding
  sphere, reverse-Z infinite far plane); opaque draws sorted by (pipeline, mesh, range, material,
  splat settings) and grouped; see-through draws one each, back to front; gizmos one each, in
  order; per shadow cascade, the casters that reach it, grouped the same way.
- **Levels of detail** (`noor/game/model_lod.h`): `read_model_lods` takes the levels from the
  manifest (only with distances, only consecutive levels whose files exist: a bad manifest never
  makes a model lose detail or vanish); `choose_lod` picks the first level whose distance covers
  `distance × tan(fov/2) / tan(30°) / scale`. The scene view records each mesh's model root, so
  the player (`SceneRenderer`) and the editor viewport both switch whole models; picking in the
  editor still uses level 0.
- **Cutout textures:** mipmaps of a cutout material's base colour keep their alpha coverage (each
  mip's alpha scaled so the same share of texels passes the cutoff), so foliage doesn't thin out
  with distance.
- **Measured, not assumed:** `docs/rendering/MEASUREMENTS.md` has the forest numbers.

## Consequences
- The 2,000-oak forest: **60 FPS** (vsync), CPU about 9 ms a frame, GPU 2.5 ms, 24 draw calls
  (from 4,784), 0.43 million triangles (from 30.5 million). The gate holds.
- Every scene benefits without changes: the golden images stayed identical.
- Stats change meaning: `draw_calls` counts GPU draws (one draws many instances); `triangles`,
  `instances` and `culled` count what the camera sees.
- The CPU is now the larger cost, and most of it is reading 10,000 entities' JSON components
  every frame (one entity per tree part). Part 7's instance sets (one record per tree, no
  entities) are the real fix for forests; a component cache would help entity-heavy scenes.
- No hysteresis: a model exactly at a switching distance can flip between levels as the camera
  moves. No cross-fading (levels pop). Both are known, visible only up close to the boundary,
  and left for when they bother someone.
- Edits to a placed model's node entities show at level 0 only (lower levels come from files).

## Unresolved
- Persistent instance buffers for static sets (option B) if per-frame uploads ever show in profiles.
- GPU culling and indirect draws (option C) for much larger worlds.
- Impostors and level cross-fades.
