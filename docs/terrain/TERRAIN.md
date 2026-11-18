# Terrain

Milestone 6, part 5 ([ADR-031](../decisions/ADR-031-terrain-heightfields.md)). What works today:
heightfield terrain generated from a seed or a heightmap picture, **sculpted by hand** (part 6), drawn
in the editor and in games, collided with by Jolt. Painting (part 7) is **planned**, not here yet.

## The `terrain` component

```json
"terrain": {
  "tiles": [2, 2],          // tiles along X and Z (1..16 each)
  "cell_size": 1,           // metres between samples (0.05..64)
  "base": "noise",          // "noise" | "heightmap" | "flat"
  "seed": 7, "height": 40, "scale": 250, "octaves": 5,     // noise: 0..height metres, features ~scale m
  "heightmap": "<asset guid>", "min_height": 0, "max_height": 50,  // heightmap: black..white
  "flat_height": 0,         // flat
  "sculpt": "<.noorsculpt guid>",  // hand-sculpted offsets: made by the first stroke (see Sculpting)
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

## Sculpting (Milestone 6, part 6)

Select a terrain, press **Sculpt** in its Inspector card, pick a tool and drag on the ground with the
left mouse button. A circle shows the brush on the ground.

| Tool | What it does | Strength means |
|---|---|---|
| Raise | pushes the ground up | metres per second at the brush's centre |
| Lower | pushes it down | metres per second |
| Smooth | evens out bumps (towards the average of each sample's neighbours) | how fast: 1 = all the way in a second |
| Flatten | levels towards the height where the stroke began | metres per second |

**Radius** in metres; **falloff** 0 is a hard edge, 1 is soft from the centre out (smoothstep).
Each dab counts at most 0.25 s, so a stalled frame can't build a mountain.

**The base never changes.** Brushes write a separate layer of height offsets, added on top of the
generated base. Change the seed, the size of the hills or the heightmap, and the hand-made edits
stay where they were, on top of the new ground. (That's the "non-destructive" part of the spec.)

- **Undo:** a whole stroke is one undo step ("Sculpt: Raise"); it stores only the rectangle the
  stroke touched (before and after), not the terrain.
- **The layer file:** the first stroke on a terrain creates `Assets/Terrain/<entity name>.noorsculpt`
  (an asset, importer `terrain_sculpt`) and sets the component's `sculpt`. Save writes it. Only the
  tiles that were sculpted are stored: 264 KB per tile (257 × 257 floats). Binary, little-endian:
  `"NSCULPT1"`, version 1, tiles x/z, tile count, then per tile its (x, z) and its offsets.
- **Play** collides with the sculpting as it is in the editor, saved or not. Games (`noor_cli build`)
  load the saved file.
- **Changing size:** a terrain that grows keeps its sculpted tiles; tiles outside a smaller terrain
  stay in the file but aren't used.
- **Duplicating** a sculpted terrain makes both use the same layer file (sculpting one changes both).
  Not handled yet: give the copy a fresh layer by removing its `sculpt` field.
- Not yet: the plan's "stretch" brushes (noise, ramp), a brush strength curve per pen pressure,
  sculpting holes or overhangs (heightfields can't).

**In a Debug build** of the editor a stroke is slow on big terrains: rebuilding a tile's mesh takes
~33 ms in Debug (2.3 ms in Release), and a brush on a tile corner rebuilds four tiles. Only changed
tiles are rebuilt; updating part of a tile is a later optimisation.

## Where the code is

- `engine/terrain` (`noor_terrain`, runtime-safe): `Settings`, `Heights` (with a shared cache keyed by
  the settings, so the renderer and physics generate once), `tile_mesh`, `fractal_noise`.
- `engine/game`: the scene view lists terrains; `SceneRenderer` draws them; physics gets one static
  `HeightFieldShape` body per tile.
- `engine/terrain_edit` (`noor_terrain_edit`, editor and tools only, kept out of games by the
  runtime-boundary test): the brushes.
- `engine/editor_api`: `Document::sculpt` (strokes, the undo command, the live layer per sculpt asset,
  saving), `viewport.cpp` (tile meshes rebuilt per changed tile, the brush circle, stroke events).

## Tests

`test_terrain` (determinism + pinned hash, noise range, seams, height lookup vs. mesh, heightmap
import, settings, cache); `golden_Terrain` (rendered hills); `cli.build_terrain` + `player.terrain_walk`
(a built game: a character walks up a heightmap ramp and its feet stay on it); editor smoke checks
(Create ▸ Terrain draws a tile, a new seed rebuilds it, deleting it frees it).

Sculpting: `test_sculpt` (brush weights, raise/lower leave the base alone, flatten, smooth, a hill on
a tile corner stays seamless, a new seed keeps the hill, file round trip and damaged files, ray casts);
`api: document api: sculpting…` (a stroke through the editor API is one undo step, makes and saves the
layer, survives a new base, Play collides with unsaved sculpting); `cli.build_sculpt` +
`player.sculpted_hill` (a built game: a character walks over a hill that exists only in the sculpt
layer, feet within 1 cm of it); editor smoke checks (a test pointer drags the Raise brush, undo, save).

## Not yet

Painting, holes, LOD, streaming, triplanar texturing for cliffs, partial tile updates while
sculpting.
