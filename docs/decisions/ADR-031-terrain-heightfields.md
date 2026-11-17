# ADR-031: Terrain: tiled heightfields with a non-destructive layer stack

**Status:** Proposed (Milestone 6, part 5 decides the details) · **Date:** 2026-11-17 · **Milestone:** 6

## Context
Spec 13.1 starts terrain as heightfield tiles and requires the representation to be documented:
height precision, cell size, axes, coordinate frame, vertical scale, chunk ids and seams. Spec
13.3 asks for a non-destructive stack, so manual sculpting survives regenerating the procedural
base. Milestone 6's gate needs a small landscape to sculpt, paint and walk on. Thornwake (our own
Godot RPG) already proved a sparse copy-on-write offset layer for terrain edits read by every
consumer (its decision D23), and the need for deterministic maths for seeded generation (D5).

## Options
- **A: Tiled heightfields** (regular grid per tile, float heights) + layers. Simple to render,
  collide (Jolt `HeightFieldShape`), sculpt and store; no overhangs or caves (those are voxels,
  spec 14).
- **B: One big heightfield.** Simplest, but no streaming, slow undo/save for big maps, and seams
  become a problem the day we tile it.
- **C: Voxels/SDF first.** Caves and overhangs, but far more work for rendering, collision and
  painting; spec 14 is its own milestone.

## Decision (proposed)
**A.**
- **Frame:** right-handed, +Y up, metres (ADR-022). A terrain entity's transform places tile
  (0, 0)'s corner; tiles extend along +X and +Z.
- **Tiles:** 256 × 256 cells = 257 × 257 samples, neighbours share their edge samples (no seams by
  construction). Cell size per terrain, 1 m by default. Tile id `(x, z)` integers.
- **Heights:** 32-bit float metres in memory and in the sculpt layer; the base can come from a
  16-bit PNG with a stated vertical range (`min`, `max` metres). Precision documented per source.
- **Layers, bottom to top:** base (imported heightmap or seeded generator; a recipe + seed, so it
  can be regenerated), **sculpt offsets** (sparse: only touched tiles stored; added to the base),
  **material weights** (up to 4 materials per terrain in Milestone 6, one RGBA8 weight map per
  tile). Later layers (roads, rivers, foundations, spec 13.5) slot in between as typed modifiers.
- **Files:** `Assets/Terrain/<name>/terrain.json` (frame, cell size, layers, materials, recipe)
  + per-tile binary files for sculpt and weight layers; compiled tiles go to `.noor/cache` and into
  builds. Editing writes only touched tiles.
- **Generation** uses deterministic noise built from + − × ÷, sqrt and floor (no `sin`/`exp`), so
  a seed gives the same terrain on every machine.
- **Runtime:** compiled heights + weights per tile; render meshes per tile (one LOD in Milestone 6);
  Jolt height fields per tile.

## Consequences
- Easier: sculpt/paint as image-like operations on tiles; region undo; partial saves and rebuilds.
- Harder: no overhangs, caves or vertical cliffs (steep slopes stretch textures until triplanar
  arrives); tile counts limit map size until LOD/streaming (later).
- Must: document the frame in `docs/terrain/`, test seams (neighbour edges identical) and determinism
  (same seed → same hash) in CI.

## Evidence / benchmark
None yet. Part 5 measures memory, compile and render cost for 2 × 2 tiles (512 m) and sculpt
latency.

## Migration or reversal strategy
Layers are stored separately from compiled data, so the tile size or storage format can change
with a migration (ADR-005) that resamples layers. A voxel layer (spec 14) can be added beside the
heightfield later rather than replacing it.

## Reviewed by
Sage (proposal).
