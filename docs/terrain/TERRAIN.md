# Terrain

Milestone 6, part 5 ([ADR-031](../decisions/ADR-031-terrain-heightfields.md)). What works today:
heightfield terrain generated from a seed or a heightmap picture, drawn in the editor and in games,
collided with by Jolt. Sculpting (part 6) and painting (part 7) are **planned**, not here yet.

## The `terrain` component

```json
"terrain": {
  "tiles": [2, 2],          // tiles along X and Z (1..16 each)
  "cell_size": 1,           // metres between samples (0.05..64)
  "base": "noise",          // "noise" | "heightmap" | "flat"
  "seed": 7, "height": 40, "scale": 250, "octaves": 5,     // noise: 0..height metres, features ~scale m
  "heightmap": "<asset guid>", "min_height": 0, "max_height": 50,  // heightmap: black..white
  "flat_height": 0,         // flat
  "material": "<.noormat guid or null>",
  "collision": true
}
```

Create ▸ Terrain adds one with noise defaults. The Inspector shows `base` as a list and
`heightmap`/`material` as pickers. Missing fields take the defaults above.

## The frame

- Right-handed, +Y up, metres (ADR-022), like everything else.
- **Centred on the entity:** a 2 × 2 terrain with 1 m cells spans −256..+256 m in X and Z around the
  entity's position. The entity's rotation and scale are **ignored** (heightfields stay axis-aligned
  so collision, sculpting and painting can stay simple).
- **Tiles:** 256 × 256 cells = 257 × 257 samples. Neighbours share their edge samples, so there are no
  seams (tested: positions and normals on shared edges are bit-identical). Tile `(0, 0)` is at −X, −Z.
- **Triangles:** each cell is split along the diagonal from its (x, z) corner to its (x+1, z+1)
  corner. That's the same split Jolt's `HeightFieldShape` uses, so what you see is what you stand on.
  `Heights::height(x, z)` interpolates on those same triangles (tested against the mesh).
- **UVs** are world X/Z in metres (a material with 1 m tiling repeats once per metre).

## Heights

- 32-bit float metres in memory. A heightmap is read as 16-bit when the PNG is 16-bit (65536 steps:
  0.8 mm over a 50 m range); 8-bit pictures give 256 steps. The picture is resampled to the terrain's
  sample grid (bilinear), image left = −X, image top = −Z.
- Jolt stores height fields slightly compressed; in the walking test, feet are within 2 mm of the
  exact height.

## Determinism

Noise is our own gradient noise: SplitMix64 integer hashing, a quintic fade, and only + − × ÷ and
floor, so no library maths that differ between compilers or CPUs. The same settings give the same
heights, bit for bit. `test_terrain` pins the hash for seed 7 (measured identical in Debug and
Release with MSVC x64). If that hash changes, existing terrains look different: change it only on
purpose and say so in the changelog.

## Cost (measured 2026-11-18, RelWithDebInfo, the dev machine)

| | Time | Memory |
|---|---|---|
| Heights for 2 × 2 tiles (513² samples, noise, 5 octaves) | ~35 ms | 1.05 MB |
| One tile's render mesh (66 049 vertices, 393 216 indices) | ~2.3 ms | 5.5 MB on the CPU (and on the GPU) |

The mesh is fat (full vertex format, one LOD). LOD and a lighter terrain vertex format come later;
sixteen tiles would be ~90 MB of meshes today.

## Where the code is

- `engine/terrain` (`noor_terrain`, runtime-safe): `Settings`, `Heights` (with a shared cache keyed by
  the settings, so the renderer and physics generate once), `tile_mesh`, `fractal_noise`.
- `engine/game`: the scene view lists terrains; `SceneRenderer` draws them; physics gets one static
  `HeightFieldShape` body per tile.
- `engine/editor_api/src/viewport.cpp`: the editor's tile meshes (rebuilt when the settings change).

## Tests

`test_terrain` (determinism + pinned hash, noise range, seams, height lookup vs. mesh, heightmap
import, settings, cache); `golden_Terrain` (rendered hills); `cli.build_terrain` + `player.terrain_walk`
(a built game: a character walks up a heightmap ramp and its feet stay on it); editor smoke checks
(Create ▸ Terrain draws a tile, a new seed rebuilds it, deleting it frees it).

## Not yet

Sculpting, painting, holes, LOD, streaming, triplanar texturing for cliffs, saving terrains as
asset files (they live in the component until there are sculpt and weight layers to store).
