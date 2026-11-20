# Milestone 6 gate review: manual creation tools

**Date:** 2026-11-18 · **Reviewed by:** Sage (engineering) · Tom and Noor to look at the sample.

**Goal (spec):** enable meaningful level creation directly in the editor.
**Verdict:** the gate passes, with the exceptions below written down rather than hidden.

## Deliverables

| Deliverable (spec) | Status | Evidence |
|---|---|---|
| Brush primitives | ✅ | Box, wedge, cylinder, cone, sphere, stairs, arch as `brush` components (ADR-016); Create ▸ Brush; numeric fields in the Inspector |
| CSG operations | ✅ | Add/subtract/intersect through Manifold v3.5.4 (Apache-2.0); child order is the operation order; see-through cutters; 100 cuts in ~14 ms (part 1 spike: every nasty case passed) |
| Surface materials | ✅ | A material and UVs per brush face (world or brush aligned), click a face to pick it, texel density shown; Paint tool clicks faces too |
| Collision cooking | ✅ | Cooked `.ncsg` per model (editor on Save/Play, `noor_cli build`/`cook`); mesh colliders from the cooked triangles; terrain tiles as Jolt height fields |
| Mesh export | ✅ | Export GLB… writes Noor Interchange v0.3 (embedded textures, a `-colonly` collision node, a `.noor.json` manifest) |
| Material/UV painting baseline | ✅ (baseline) | Terrains: four material layers blended by painted weights (splat shader); CSG faces: the same Paint tool by click. No texture painting on arbitrary meshes (planned later) |
| Terrain sculpting basic brushes | ✅ | Raise, lower, smooth, flatten on a sparse layer over a generated base (ADR-031), a brush circle on the ground, fast strokes filled in |
| Undo and asset integration | ✅ | Every brush edit and every stroke is one undo step (strokes store only the touched rectangle); sculpt and paint layers are assets (`.noorsculpt`, `.noorpaint`) packed by `noor_cli build` |

## Gate criteria

| Gate (spec) | Result | Evidence |
|---|---|---|
| Build a small house and landscape with in-editor tools | ✅ | **Hillside** (`samples/Hillside`): a sculpted, painted landscape and a brick house from brushes, made by `tools/samples/build_hillside.py` through the editor API (the functions the editor's buttons call: materials, terrain, sculpt and paint strokes, brushes, save). The same tools are driven through the GUI by the editor smoke test (a test pointer sculpts, paints a terrain and a CSG face). See exception 1 |
| Save/reopen | ✅ | API tests save and reopen brushes, sculpting (with undo/redo across a save), painting and terrain settings; the smoke test reads the saved layer files back; the sample reopens in a fresh copy (`api: …Hillside sample opens…`) |
| Run character collision | ✅ | Built games: `player.hillside_walk` (the walker follows the path, through the doorway, and ends inside the house on its floor: (19.99, 5.71, 2.38)); `player.csg_doorway`, `player.terrain_walk`, `player.sculpted_hill` (feet 8 mm from the exact hill height). Play collides with unsaved sculpting (API test) |
| Export meshes | ✅ | The sample's house exports: the GLB reads back with a collision node and the same 184 triangles; manifest fields and hash checked in the unit tests. See exception 2 |
| No destructive editor dependency leaked into runtime | ✅ | `boundary.runtime_has_no_editor_code` walks the link graph at configure time: `noor_player` and the runtime modules link neither `noor_csg`, Manifold nor `noor_terrain_edit` (and the walk proves it can see them, in the unit tests) |

## What the sample found (fixed in part 8)

1. **No way to ask the ground's height.** Placing the house on the sculpted pad, and the walker on
   the path, needed the terrain's height at a point. Added `noor_doc_terrain_height` (editor API
   v19), with tests; the editor can use it for dropping things onto the ground.
2. **Strokes add up.** A ridge painted as a line of dabs 3 m apart under a 28 m brush gets each
   spot about 17 times per pass: my first "ridge" was 50 m tall and looked like a volcano. Not a
   bug (it's how an airbrush works), but it's documented now in TERRAIN.md's numbers, and it
   points at a better brush model later (flow per distance, not per time).
3. **Poly Haven's Leafy Grass is autumn-tan** (its average colour is RGB 151, 131, 89). The meadow
   is a material instance with a greener base colour, which is how an artist would do it in the
   editor too.
4. **Stale binaries, twice.** Two checks today ran an old player (one a copy inside a test's work
   folder, one an un-rebuilt RelWithDebInfo build) and showed pictures without the new features.
   The tests themselves always use the freshly built targets; the lesson is about hand checks
   (saved to memory: check the binary's time before trusting a picture).

Found earlier in the milestone and fixed there: `;` in test names that made ctest run nothing
(now a configure error); the CSG model that was solid but invisible (now checked by pixels); the
ground grid z-fighting with flat terrains; dotted lines from fast strokes; two smoke checks that
depended on frame timing (a sculpt height bound, and the Render panel's pass timings, which flaked
once in the final full run and passed on the rerun; it now waits for them).

## Exceptions (accepted, tracked)

1. **The sample was built by a script through the editor API, not by hand in the GUI.** Every tool
   it uses is also driven through the GUI by the smoke test, but nobody has yet built a whole
   house and landscape by hand with a mouse. That's the real usability test, and it's Tom's.
2. **Exports are checked by our own importer only.** The GLB has not been opened in Blender or
   Godot by a test, and the Khronos glTF validator isn't wired in (the manifest says
   `"gltf_validator": "not_run"`).
3. **Terrain limits:** one level of detail and up to 16 × 16 tiles; tile meshes rebuild whole (slow
   strokes in Debug builds: ~33 ms per tile); painted edges are about one cell (1 m) soft; no
   height-based blending, triplanar mapping, holes or caves; duplicated terrains share layer files.
4. **Sculpting extras not done:** the plan's stretch brushes (noise, ramp); undo memory is
   per-rectangle but not capped or reported (the plan's risk 3 asked for both).
5. **CSG editor gaps:** materials are picked in the Inspector (no drag and drop onto faces); the cut
   updates when a drag ends, not during it; no vertex/edge/face modelling (spec 12.3, planned later).
6. **One GPU tested** (NVIDIA RTX 3080, Windows 10), as in Milestone 5.
7. **Carried from earlier milestones:** LOD loading, device-loss recovery, prefab child edits, the
   automated HiDPI run, script API gaps, bloom, reflection probes and lamp shadows.

## Measurements
- Tests: **227/227** (Debug, incl. GPU, editor, samples and three built games for Milestone 6).
- Golden-image scenes: 16 (13 from Milestone 5 + Terrain, TerrainPaint, Hillside).
- CSG: 100 boolean cuts in ~14 ms (Manifold, part 1); the sample's house is 184 triangles.
- Terrain (RelWithDebInfo): heights for 2 × 2 tiles ~35 ms; a tile mesh ~2.3 ms and 5.5 MB; a
  sculpt or paint layer 264 KB per touched tile.
- Hillside: 15 BC7/BC5 texture maps (20 MB on the GPU), one terrain tile, one CSG model.

## Milestone tradition: Alden discovers something
Proposed line for the Milestone 6 picture (Tom and Noor make it):
*"Alden discovers building: he raises a hill, paints a path, and walks it home."*

![Alden three times over: raising a hill with a sweep of golden light, walking a winding path he made, and building a house frame](../devlog/img/alden/alden_m6_building.webp)

*Picture: Noor (2026-11-20), for the milestone tradition (Noor's idea, devlog #015).*

## Carried into Milestone 7 (procedural graph and first generation tools)
- The terrain's layer stack is the natural first client of the procedural graph: the generated
  base is already a recipe (seed + settings), and the gate's "local override survives
  regeneration" already holds for sculpting and painting.
- Noor Interchange: generated assets (Tom's NoorMD trees are waiting in `test_tree/`, locally) need
  the `_LOD0` naming reconciled with the registry's `.lod0` convention.
- A brush model with flow per distance (finding 2), and partial tile updates for faster strokes.
