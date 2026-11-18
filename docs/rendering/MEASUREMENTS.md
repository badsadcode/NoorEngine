# Rendering measurements (Milestone 5, part 7)

The plan said part 7 decides two things by measuring, not guessing: texture compression (BC7/BC5)
and one shared GPU device for the editor's viewports. These are the numbers and the decisions.

**How:** `noor_player` (RelWithDebInfo) with `--frames 240 --msaa N --report`. GPU times come from
the renderer's timestamp queries (`render.gpu_ms`, `gpu_passes`), median of 3 runs. 1280×720,
NVIDIA GeForce RTX 3080, Windows 10, 2026-11-17. The script lives with the dev notes, not the repo.
It is easy to re-run: the report has every number in its `render` section.

**Caveat:** these scenes are tiny for this GPU. With vsync it runs at low clocks, so single
numbers wobble by ±0.5 ms between runs (one 4× run came out slower than its 8× run). Read the
table for orders of magnitude, not decimals.

## GPU time per frame (ms)

| Scene | Draws (shadow) | MSAA off | 4× | 8× | Biggest pass |
|---|---|---|---|---|---|
| render_scenes/Materials | 37 (28) | 1.0 | 1.4 | 2.2 | Shadows 0.6–1.3 |
| render_scenes/Glass (4 see-through) | 37 (28) | 1.4 | 1.4 | 1.6 | Shadows 0.7–1.6 |
| render_scenes/Env_venice_sunset (12 balls, no sun) | 14 (0) | 0.2 | 2.0 | 1.5 | Scene |
| NoorFPS-lite Main | 110 (84, 12 skipped) | 1.4 | 1.3 | 1.6 | Shadows 0.9–1.6 |

- Every scene is far under the 16.7 ms of a 60 Hz frame.
- **Shadows** cost the most in every scene with a sun: four 2048² cascades are drawn every frame.
  Per-cascade culling (this part) skips casters that can't reach a cascade (12 of 96 in
  NoorFPS-lite, 113 of 264 in the editor smoke scene with the large building). The next shadow
  win, when a scene needs it, is caching the far cascades (they barely change) or smaller maps
  for them.
- **MSAA 4×** costs a few tenths of a millisecond to about 1.5 ms here: **the default stays 4×**
  (Preferences ▸ Anti-aliasing in the editor, `--msaa` for the player, `RendererDesc::msaa_samples`).

## Memory

| What | Size |
|---|---|
| Frame images at 1280×720 (HDR, depth, shadows), MSAA off | 75 MB (64 MB of it the four shadow cascades) |
| … with 4× MSAA | 117 MB (+42 MB) |
| … with 8× MSAA | 159 MB |
| Textures in the test scenes and NoorFPS-lite | under 1 MB (small test textures) |
| Meshes | under 4 MB, even with the OSM building |

**Texture compression (BC7/BC5): done right after part 7** (see [TEXTURES.md](TEXTURES.md)).
Measured on a real 1254² PBR set (colour, ORM, normal) in a release player: **8 MB → 2 MB per
map** (28 MB → 10 MB for the scene, the rest being the HDRI background), about 1 s to compress
the three maps the first time, then loads as fast as before from the cache. No visible
difference (mean 0.06/255 per channel). The reasoning that led there: the tests don't use
big textures, so measuring them says nothing. The arithmetic for real assets does (this is a
calculation, not a measurement): one 2048² map with mips is 21 MB as RGBA8. A typical material
has three (colour, normal, ORM): **64 MB per material**. Twenty such materials, a modest level of
The Uncounted, Chapter Two, would be 1.3 GB, too much for many GPUs. BC7 (colour, ORM) and BC5
(normals) store 1 byte per pixel instead of 4: 16 MB per material. It needs a CPU encoder (a
licence-checked dependency) and a cache format change, so it got its own step before any game
used big texture sets.

**One shared GPU device for editor viewports: not needed yet.** The editor now runs two at most
(the scene and the material preview). The preview loads only its own material's textures and
its frame images are small. Revisit when the Content Browser gets material thumbnails or several
previews are open at once.

## A forest: instancing and levels of detail (Milestone 7, part 6)

**What:** 2,000 NoorForm oaks (`tests/data/noorform_oak`: four levels, 15,268 / 12,256 / 5,178 / 8
triangles) on a 900 m square, a sun with shadows, the camera at the edge at eye height. 10,003
entities (each oak is a root and four node entities, as the editor places a model). Built with
`python tools/samples/build_forest.py <folder> --trees 2000 --area 900 --name Forest2000` (not
committed: 6.5 MB of scene).

**How:** `noor_player` (RelWithDebInfo) `--frames 300 --report`, MSAA 4×, 1280×720, vsync on (60 Hz),
NVIDIA GeForce RTX 3080, Windows 10, 2026-11-18. Times are the median of frames 60–300 (the
report's new `timing_ms`: wall time per frame; CPU split into the game, collecting the scene and
building draws, recording; GPU from timestamps). The final row is the median of three runs.

| Step | Frame | CPU: game / view (collect) / record | GPU | Draw calls | Triangles |
|---|---|---|---|---|---|
| Before part 6 | 192 ms (5 FPS) | 1.5 / 189 / 0.7 ms | 22.4 ms (p95 29.8) | 4,784 | 30.5 M |
| Scene matrices in O(n) (not quadratic) + instancing + camera culling | 16.7 ms (60 FPS) | 0.8 / 9.0 / 0.6 ms | 6.1 ms | 16 | 23.9 M |
| + levels of detail | 15.7 ms (p95 21) | 0.8 / 12.8 (11.3) / 0.3 ms | 2.8 ms | 24 | 0.43 M |
| + asset paths resolved once per frame, not per mesh; cutout mips | **16.7 ms (60 FPS, vsync)** | **1.2 / 7.7 (5.9) / 0.35 ms** | **2.5 ms** (p95 3.7) | **24** | **0.43 M** |

- **The gate holds:** 2,000 trees with four levels at 60 FPS in a Release build, with CPU (about
  9 ms) and GPU (2.5 ms) both well inside a 16.7 ms frame.
- **The first win wasn't the GPU.** 189 of the 192 ms were the CPU finding each entity's parents one
  by one, every frame (quadratic in the number of entities). Batching took 4,784 draw calls to 16.
- **Levels of detail** cut the triangles 55× (most trees are 72–180 m away: level 2, 5,178 triangles;
  beyond that, an 8-triangle billboard). Shadows are now the biggest GPU pass (about 2 ms of 2.5).
- **What's left is CPU, and it's the scene's shape:** reading 10,000 entities' JSON components each
  frame costs about 6 ms. Part 7 places forests as instance sets (one record per tree, no entities),
  which removes most of it. Until then, a few thousand placed models are fine; tens of thousands
  aren't.
- One of the three final runs had a p95 of 24 ms (single hitches, not a slow average); the other two
  17.4–17.5 ms. With vsync, a frame that misses 16.7 ms waits for the next refresh.
- Memory: the instance buffers grow to what a frame needs: 64 bytes per instance, scene and shadow
  instances together (about 10,000 here, so roughly 1 MB per frame in flight with headroom; worked
  out from the counts, not read from the GPU). Textures 3.5 MB (only the levels in view are loaded),
  meshes 1.5 MB.

The committed test forest (`tests/data/forest_project`, 120 oaks) is a golden render
(`render.golden_Forest`) and a check (`render.forest_instancing_lod`): 152 objects in 10 scene draw
calls, 42 culled, 0.32 M triangles.

## The atmosphere sky (Milestone 8A, part 1)
Release build, RTX 3080, Ryzen CPU (16 threads), `render_scenes/Atmosphere_Noon` and `Atmosphere_Sunset`:

| What | Time |
|---|---|
| Atmosphere tables (transmittance 256×64, multiple scattering 32×32), once per setting | about 0.1 s |
| A sun change: sky image 512×256 (40-step march per pixel, all cores) + prefiltered cubemap and irradiance | **about 120 ms** (CPU) |
| The scene pass at 1280×720 with the sky and its sun disc (7 objects) | 0.09 ms (GPU) |

The sky pass is one texture lookup and the disc test per pixel; scaled by pixel count it stays under
0.21 ms at 1920×1080 (not measured there: the player can't render at 1080p yet). The 120 ms is why the sun
can't move smoothly yet: part 2 moves the tables and the lighting to the GPU.

## Not measured yet
A scene with hundreds of textured materials, integrated GPUs, and 4K screens (frame images scale
with pixels: about 4× the numbers above at 3840×2160).
