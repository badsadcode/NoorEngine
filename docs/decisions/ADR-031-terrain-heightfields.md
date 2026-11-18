# ADR-031: Terrain: tiled heightfields with a non-destructive layer stack

**Status:** Accepted (Milestone 6, part 5; the layer files are settled in parts 6–7) · **Date:** 2026-11-17, accepted 2026-11-18 · **Milestone:** 6

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

## What part 5 settled (2026-11-18)
Built and tested; where it differs from the proposal above, this wins.
- **Settings live in the `terrain` component**, not in `Assets/Terrain/<name>/terrain.json`: a seed
  or heightmap recipe is a few numbers, and a file would only add a second place to keep in sync.
  The per-tile binary files arrive with the first data that needs them (sculpt layer, part 6;
  weights, part 7).
- **Centred on the entity** (not "the entity is tile (0, 0)'s corner"): Create ▸ Terrain at the
  origin puts you in the middle of it. Rotation and scale of the entity are ignored.
- **Triangle split = Jolt's** (diagonal from the cell's (x, z) corner to (x+1, z+1)), in the mesh and
  in `Heights::height`, so the drawn ground is the collided ground (built-game test: feet within
  2 mm).
- **One Jolt `HeightFieldShape` body per tile** (257 samples; Jolt pads internally), static.
- **Noise** uses + − × ÷ and floor only (no sqrt either), SplitMix64 hashing; seed 7's hash is pinned
  in CI and was identical in Debug and Release builds (MSVC x64). Other compilers: checked when we
  build with them.
- **Shared heights:** renderer and physics share one generated `Heights` per settings key.

## Consequences
- Easier: sculpt/paint as image-like operations on tiles; region undo; partial saves and rebuilds.
- Harder: no overhangs, caves or vertical cliffs (steep slopes stretch textures until triplanar
  arrives); tile counts limit map size until LOD/streaming (later).
- Must: document the frame in `docs/terrain/`, test seams (neighbour edges identical) and determinism
  (same seed → same hash) in CI.

## Evidence / benchmark
Measured 2026-11-18, RelWithDebInfo, the dev machine ([docs/terrain/TERRAIN.md](../terrain/TERRAIN.md)):
heights for 2 × 2 tiles in ~35 ms (1.05 MB); one tile mesh in ~2.3 ms (5.5 MB: full vertex format,
one LOD, so a lighter terrain format is worth doing before big maps). Render cost per frame was not
measured separately (the Render stats panel shows it live). Sculpt latency: part 6.

## Migration or reversal strategy
Layers are stored separately from compiled data, so the tile size or storage format can change
with a migration (ADR-005) that resamples layers. A voxel layer (spec 14) can be added beside the
heightfield later rather than replacing it.

## Reviewed by
Sage (proposal; accepted after part 5).
