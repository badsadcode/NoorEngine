# Milestone 7 plan: procedural graph and first generation tools

**Goal (spec):** implement reusable generation infrastructure before many specialised generators.

**Deliverables (spec):** typed procedural node graph, cache/invalidation, seeds, progress, headless
operation; then terrain, trees and rocks as selected first real clients.

**Gate (spec):** given fixed inputs and versions, outputs are reproducible; a local override
survives regeneration; changed regions invalidate only appropriate results; output can be used
outside the generator.

**Where we start:** terrain already has a generated base (seed + settings, deterministic noise) with
hand-made layers on top that survive regeneration (sculpt, paint: ADR-031), so one gate criterion
already holds for one generator. There is no graph, no shared cache, no job system with progress,
no `noor_cli generate`, no instancing (every object is its own draw) and no LOD switching. Trees and
rocks are made in NoorMD (NoorForm, C#), which exports GLB packs with LODs, recipes and seeds; nine
oaks from it are on the dev machine (`test_tree/`, local only).

## Decisions
- **ADR-014 Procedural graph** (Proposed, below and in [ADR-014](../decisions/ADR-014-procedural-graph.md)):
  graphs are assets (`.noorgraph`), nodes are typed functions registered in the engine, evaluation
  is pure and content-addressed (a node's result is cached under the hash of its version, parameters,
  seed stream and inputs), hand-made overrides are layers the graph never writes, and every graph runs
  headless (`noor_cli generate`). Games load **outputs**, not graphs (like cooked CSG).
- **Studio boundary (no new ADR; the studio rule and ADR-023):** NoorMD makes trees and rocks;
  NoorEngine imports them **as files** (Noor Interchange) and places them. We don't call NoorMD's
  code and we don't build a second tree generator. A graph node can wrap an external headless
  generator later, through a process + files contract, once one exists.
- **ADR-032 Instanced rendering and LOD** (to propose in part 6): instance buffers per mesh, LOD
  chosen per instance by distance (the asset's own distances when it has them), shadows for instances.
- **ADR-007 plugin ABI stays open.** Part 1 designs the node registry so a plugin can register nodes
  later (C ABI, versioned, no STL across the boundary), but Milestone 7 ships only built-in nodes.
  Tom's generators (BadShip, BadBuilding, NoorForm) plug in through files until ADR-007 is decided.

## How the pieces fit

```text
 .noorgraph asset (nodes, typed pins, parameters, seed)        noor_cli generate <project> <graph>
        |                                                                |
        v                                                                v
 noor_procgraph (editor + tools): validate -> schedule -> evaluate nodes on the job system
        |        per node: key = hash(node type + version, params, seed stream, input keys)
        |        cache hit? reuse .noor/cache/procgraph/<key>   miss? run, store, trace (ms, bytes)
        v
 outputs (typed): HeightField tiles, Masks, InstanceSets, asset references
        |
        +--> terrain base layer (ADR-031): the graph replaces "noise | heightmap | flat";
        |    sculpt and paint layers stay on top, untouched by regeneration
        +--> instance sets (trees, rocks): placement from terrain + masks; a hand-made
        |    add/remove layer on top, untouched by regeneration
        +--> files other tools can read: heightmap PNG/RAW, instance set JSON (Noor Interchange)
        v
 cooked into games by noor_cli build (no graph code in the player: boundary test)
```

## Parts (each one ends tested, documented and pushed)

| Part | What | Proves |
|---|---|---|
| 1 | **Graph core** (ADR-014): `noor_procgraph` module; `.noorgraph` JSON (nodes, pins, links, parameters, graph seed, format version); typed pins (scalar, vector, seed, height field, mask, instance set, asset reference) with units; node registry (type, version, pins, parameter schema); validation (cycles, missing inputs, type and unit mismatches, unknown node versions: errors on the node, the graph still loads); deterministic evaluation with per-node seed streams (graph seed + node id, never a shared RNG); a first set of nodes: constant, math, noise, combine, clamp/remap | The same graph and inputs give the same output hash every run, in Debug and Release; a cycle or a missing input is an error message on the right node, not a crash. **Done**: `noor_procgraph`, six nodes, fingerprint pinned (Debug = Release), every mistake on its node ([PROCGRAPH.md](../procgraph/PROCGRAPH.md)) |
| 2 | **Cache, invalidation, jobs, headless**: content-addressed results in `.noor/cache/procgraph` (keyed by node version + params + seed stream + input keys); only changed nodes and what depends on them rerun; a job system with progress and cancellation (background threads, ordered results so threads can't change outputs); execution trace (ms and bytes per node, cache hits); **`noor_cli generate`** writes outputs and a fingerprint file | Change one parameter: exactly that node and its dependants rerun (counted in a test); cancel mid-run and nothing half-written is kept; the CLI and the editor produce identical bytes. **Done**: cache (memory + files), threads, progress, Cancel, `noor_cli generate`, editor API v20; all three proofs are tests. No cache size limit yet |
| 3 | **Terrain as a client**: height-field nodes (noise, heightmap import, terrace, erode-lite *(stretch)*, stamp with bounds), mask nodes (slope, height band, paint layer, distance to a path/footprint); the terrain component's base can be a graph output (`"base": "graph"`); **tiles as regions**: a node with bounds only dirties the tiles it touches | Regenerate with a new seed: sculpt and paint survive (already true) **and** a stamp moved in one corner recomputes only that corner's tiles (tile hashes compared in a test). **Done**: tiles as regions, stamp/terrace/height band/slope/heightmap nodes, terrains on graphs (cooked for games); both proofs are tests, plus a built game standing on a graph's terrain. Erode-lite and distance-to-path masks not done (later) |
| 4 | **Graph editor MVP** in NoorEditor: a canvas with nodes, typed coloured pins and wires; add/delete/move nodes, connect/disconnect (undoable: graph edits are document commands); node parameters in the Inspector; Run with progress and Cancel; errors and timings shown on nodes; a 2D preview of height fields and masks | A graph built and edited in the editor, saved, reopened, run, undone; the smoke test drives it like a user |
| 5 | **Generated assets in, Noor Interchange**: import trees and rocks from NoorMD's NoorEngine export mode ([guide](../interchange/NOORMD_NOORENGINE_EXPORT.md): `<name>.glb` + `.lodN.glb` + `.noor.json` v0.4) as one asset with its levels, `max_distance`s, materials, recipe, seed, family and provenance kept and shown; wind data kept but not used yet; a shared fixture with NoorMD's tests | One oak variant imports as one asset with four LODs, and its manifest's distances, recipe and seed are visible in the editor |
| 6 | **Instanced rendering and LOD** (ADR-032): instance buffers (one draw per mesh part per LOD, not per object), per-instance LOD by distance, alpha-cutout two-sided foliage verified, instanced shadows (culled per cascade), measured on a forest scene: CPU, GPU per pass, draws, memory (spec 16.3) | 2,000 trees (four LODs) at 60 FPS in a Release build on the dev GPU, with numbers in MEASUREMENTS.md; a golden render of a small forest |
| 7 | **Scatter (NoorVegetation-lite)**: an instance-set node: candidate points by deterministic Poisson-disk sampling (seed stream), filtered by masks (slope, height, paint layer weight, exclusions from paths and CSG footprints), per-species density and scale/rotation ranges; a **hand-made layer** (add, remove, move instances in the viewport) that regeneration never touches; trunk collision (capsules from the asset's bounds) *(stretch)*; export of an instance set as a file another tool can read | Change the seed: the forest rearranges, the trees you placed or deleted by hand stay placed or deleted; the exported instance set loads in a tiny standalone reader (outside the generator) |
| 8 | **Sample + review**: Hillside grows a forest (oaks and rocks from NoorMD, placed by a graph around the house and off the path), a meadow mask from the paint; the gate tests (reproducible fingerprint across runs and CLI/editor, override survival, region invalidation, an output used outside); built game walk through the forest; Milestone 7 review | The gate |

## Highest risks (settle early)
1. **Determinism with threads and caches.** Parallel evaluation must not change bytes; floating-point
   sums must have a fixed order; the cache key must include everything that affects a result
   (node version above all). Part 1 pins fingerprints in Debug and Release like the terrain hash.
2. **Scope of the graph editor.** Node canvases eat time (wire routing, zoom, selection, undo). Part 4
   is an MVP on purpose: no subgraphs, comments or groups yet (spec 15.1 lists them; later parts).
3. **Rendering a forest.** Every object is one draw today; a few thousand trees with LODs and shadows
   need instancing and LOD (part 6) before the sample is honest about performance. Measure first.
4. **Cross-repo contracts.** Trees come from NoorMD. Its packs use their own `Oak.manifest.json`, not
   Noor Interchange's `<name>.noor.json`; that must be reconciled by a spec proposal, not a silent
   special case. Regenerating trees from recipes inside a graph needs a headless NoorForm (it has
   none today): until then the engine records recipe + seed and imports the files.
5. **Asset size in the repository.** Today's oaks are 2–7 MB per LOD file (about 170 MB for nine),
   three quarters of it duplicated PNG textures. Tom's OK: they go in if they fit; NoorMD's
   NoorEngine export mode (JPEG, smaller far-level maps) should bring an oak to about 4 MB.

## Not in Milestone 7 (planned later)
Subgraphs/macros, comments and groups in the editor; a material graph (ADR-030's future); erosion
as a real simulation; roads, rivers, cities and buildings as graph clients (Milestone 9); GIS/DEM
import (ADR-015); wind animation for foliage; impostor baking; vegetation streaming and density LOD
for huge worlds; plugin-provided nodes (ADR-007); a headless NoorForm (NoorMD's repository).

## Assets we'll need
- A committed subset of Tom's NoorMD oaks (to agree with Tom) and a few NoorMD rocks.
- The existing Hillside sample as the landscape; its CC0 textures.
- A small hand-made `.noorgraph` set for tests (terrain, masks, scatter).

## Open questions for Tom (and Noor)
1. ~~Manifest format.~~ **Answered (Tom, 2026-11-18):** NoorMD adapts to the engine, with a new
   **NoorEngine export mode**. The changes are written up in
   [NOORMD_NOORENGINE_EXPORT.md](../interchange/NOORMD_NOORENGINE_EXPORT.md) (Noor Interchange v0.4
   added the optional fields it needs). NoorEngine imports only that layout.
2. Is a headless NoorForm (`noorform --recipe Oak_01.recipe.json --out dir`) something NoorMD wants?
   Not needed for the gate; it would let graphs regenerate trees, not just place them.
3. ~~Assets in the repository.~~ **Answered (Tom):** "if it fits, it can go to the repo." With the
   export mode's JPEG textures an oak should be about 4 MB instead of 19, so a pack fits.
