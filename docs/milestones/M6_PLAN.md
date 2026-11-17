# Milestone 6 plan: manual creation tools

**Goal (spec):** enable meaningful level creation directly in the editor.

**Deliverables (spec):** brush primitives, CSG operations, surface materials, collision cooking,
mesh export, material/UV painting baseline, terrain sculpting basic brushes, undo and asset
integration.

**Gate (spec):** build a small house and landscape with in-editor tools, save/reopen, run
character collision, export meshes, and verify no destructive editor dependency leaked into
runtime.

**Where we start:** scenes are entities with primitives (box, sphere, capsule, plane), GLB models
and prefabs; materials and textures are done (Milestone 5); Jolt has mesh colliders; every edit
is an undoable command in the engine (ADR-025). There is no geometry that the user *shapes*:
no brushes, no booleans, no terrain.

## Decisions
- **ADR-016 CSG kernel** (Proposed, settled by the part 1 spike): authoring brushes as
  parametric shapes on entities; booleans evaluated by **Manifold** (Apache-2.0) with our own
  plane-based fallback if the spike fails; materials and UVs follow source faces.
- **ADR-031 Terrain heightfields** (Proposed, settled in part 5): tiled heightfields with a
  documented frame (spec 13.1), a non-destructive layer stack (base + sparse sculpt offsets +
  material weights), Jolt height-field collision.
- **Runtime boundary** (no new ADR; follows ADR-025/028): CSG and sculpting live in editor/cook
  modules; games load **cooked** meshes and tiles. A test proves `noor_player` doesn't link them.

## How the pieces fit

```text
scene (JSON, undoable commands)
  csg_model entity ── brush children (shape, params, add/subtract/intersect, per-face materials)
        │  noor_csg (editor + cook only): brush meshes → Manifold booleans → triangles with face ids
        ▼
  cooked mesh (cache .noor/cache, packed by noor_cli build) ── render submeshes per material
                                                          └── Jolt mesh collider (cooked)
  terrain entity ── Assets/Terrain/<name>/ (descriptor + tiles: base heights, sculpt layer, weights)
        │  noor_terrain_edit (editor): brushes write the sculpt/weight layers (region undo)
        ▼
  compiled tiles (heights + weights) ── renderer (tile meshes, 4-layer splat shader)
                                     └── Jolt HeightFieldShape per tile
```

Brushes are entities, so the existing machinery gives us save/reopen, undo/redo, prefabs,
duplicate and the hierarchy for free. The order of a model's children is its **operation history**
(spec 12.2): drag a subtract brush above another and the result changes.

## Parts (each one ends tested, documented and pushed)

| Part | What | Proves |
|---|---|---|
| 1 | **CSG kernel spike + brush shapes** (ADR-016): `noor_csg` module; box, wedge, cylinder, cone, sphere, stairs, arch as closed meshes with face ids and planar/box UV projection; union/subtract/intersect through Manifold; material + UV per source face; cases: doorway in a wall, rotated and non-uniformly scaled brushes, coplanar faces, a brush fully inside another; timings | A doorway cut into a brick wall keeps the bricks and gives the new faces sensible bricks. If Manifold fails the cases, the plane-based fallback is decided here, not later. **Done**: Manifold passed every case (ADR-016 accepted); 100 cuts in ~14 ms; the runtime-boundary test is in place |
| 2 | **Brushes in the editor**: `csg_model` + `brush` components, Create ▸ Brush ▸ …, numeric parameters in the Inspector, operation per brush, hierarchy order = history, live rebuild off the UI thread, snapping (existing grid), brush outlines in the viewport | Build four walls and a roof from brushes, undo/redo every step, save and reopen. **Done**: Create ▸ Brush, Inspector lists, Alt+Up/Down reorder, see-through cutters, click selects the brush; the cut updates on release, not during a drag |
| 3 | **Surface materials and UVs**: face picking in the viewport, assign a material per face (drag from the Content Browser or the Inspector), UV offset/scale/rotation per face, world-aligned projections (planar, box), texel-density readout | Cut a window after texturing: existing faces keep their materials, new faces get the brush's. **Done**: click a face, Inspector Faces section, world/brush alignment, texel density; materials are picked in the Inspector (no drag and drop onto the viewport yet) |
| 4 | **Cook, collide, export**: CSG → cooked mesh (cache, packed by `noor_cli build`), static mesh colliders with a collision preview and a warning when they get expensive, **Export ▸ GLB** (Noor Interchange manifest), a test that `noor_player`/built games don't link the CSG or sculpt code | A character walks into the house in a built game; the house opens in Blender/Godot as GLB. **Done**: cooked `.ncsg` files (editor on save/Play, `noor_cli build`/`cook`), mesh colliders, Noor Interchange export; `player.csg_doorway` walks through a doorway in a built game. Opening the GLB in Blender/Godot is checked by our own importer, not yet by those programs |
| 5 | **Terrain foundation** (ADR-031): `terrain` component + `Assets/Terrain/<name>/`, tiles (257² samples, 1 m cells by default), base heights from a 16-bit PNG heightmap or a seeded generator (deterministic maths, see below), tile meshes in the renderer (one LOD first), Jolt height fields, no seams | A 512 m × 512 m landscape you can walk on, from a heightmap or a seed |
| 6 | **Sculpting**: raise/lower, smooth, flatten (stretch: noise, ramp), brush circle preview on the ground, radius/strength/falloff, edits in the sparse sculpt layer (the base stays untouched), undo by region | Sculpt a hill and a pad for the house; regenerate the base and the manual edits survive |
| 7 | **Painting baseline**: terrain material layers (up to 4 per terrain: `.noormat` materials), weight painting with the same brushes, a splat shader (blend by weights, per-layer tiling), "paint material" on CSG faces with the same tool | Grass, rock, dirt path and a stone house, painted by hand |
| 8 | **Sample + review**: a small house on a landscape, built only with in-editor tools (by hand, and as a test scene), golden renders, a built game walking through it, exported GLB checked, the runtime-boundary test, Milestone 6 review | The gate |

## Highest risks (settle early)
1. **CSG robustness** (spec ADR-016's "geometry robustness"): slivers, T-junctions, coplanar faces,
   numerical drift under rotation. Part 1 is a spike with nasty test cases before any UI exists.
2. **Interactive editing in the viewport**: face picking, brush handles and sculpting all need new
   tool modes in the native viewport (mouse capture, ray casts against CSG and terrain). Build
   on the gizmo code from Milestone 3; keep tools engine-side (ADR-025) so tests can drive them.
3. **Undo memory for sculpting**: strokes change thousands of samples. Store only touched
   regions (before/after) per stroke; cap and report the undo memory.
4. **Determinism of generated terrain**: Thornwake's lesson (its D5): `sin`, `exp` and friends
   differ between C libraries, so "same seed = same terrain" needs our own deterministic noise
   (only + − × ÷, sqrt, floor). Matters for Noor Interchange recipes too.
5. **Scope**: the spec's level-building chapter is huge (12.3 vertex/edge/face modelling, 12.5
   architectural helpers, 13.2's erosion brushes, 13.5 typed terrain modifiers, GIS). Milestone 6
   delivers the gate's foundation; the rest is listed as later work, not quietly dropped.

## Not in Milestone 6 (planned later)
Vertex/edge/face modelling (extrude, bevel, inset… spec 12.3), architectural helpers beyond
stairs and arches (12.5), navigation meshes (12.6: there is no navigation system yet), terrain
LOD/streaming beyond one level and a few tiles, erosion and spline sculpting, road/river
modifiers, GIS import (ADR-015), holes and caves, voxels (section 14), texture painting on meshes.

## Assets we'll need
- Materials from Milestone 5 (the CC0 brick, concrete, wood and metal sets) for the house.
- Grass, rock and dirt texture sets (CC0, Poly Haven) for terrain painting.
- A real 16-bit heightmap for import tests (CC0 or generated); Baltic Drive-style DEM import is
  later (ADR-015).

## Milestone tradition
Alden discovers something at the end of Milestone 6. Proposal: *"Alden discovers he can build:
he raises a hill, cuts a door into a wall, and walks into a house he made."* (Tom and Noor make
the picture.)
