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

**Texture compression (BC7/BC5): not in part 7, but planned and needed soon.** The tests don't use
big textures, so measuring them says nothing. The arithmetic for real assets does (this is a
calculation, not a measurement): one 2048² map with mips is 21 MB as RGBA8. A typical material
has three (colour, normal, ORM): **64 MB per material**. Twenty such materials, a modest level of
The Uncounted, Chapter Two, would be 1.3 GB, too much for many GPUs. BC7 (colour, ORM) and BC5
(normals) store 1 byte per pixel instead of 4: 16 MB per material. It needs a CPU encoder (a
licence-checked dependency) and a cache format change, so it gets its own step before a game
uses big texture sets.

**One shared GPU device for editor viewports: not needed yet.** The editor now runs two at most
(the scene and the material preview). The preview loads only its own material's textures and
its frame images are small. Revisit when the Content Browser gets material thumbnails or several
previews are open at once.

## Not measured yet
CPU time per frame in release builds of large scenes, a scene with hundreds of textured
materials, integrated GPUs, and 4K screens (frame images scale with pixels: about 4× the numbers
above at 3840×2160).
