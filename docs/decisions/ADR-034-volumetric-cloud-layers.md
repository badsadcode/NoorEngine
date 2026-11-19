# ADR-034: Volumetric cloud layers

**Status:** Accepted (Milestone 8A, part 7b, 2026-11-19)

## Context
Part 4's clouds (ADR-033) are thin layers: a 2D coverage map on a shell, shaded with an approximation of
thickness. They are cheap (about 27 µs a frame) and have a look of their own, wanted as it is for stylised games
(classic layered skies). For realistic scenes they read as painted on a flat surface: near the horizon they squash
into slivers, they have no lit sides or dark bellies, and nothing stands in front of anything else. Full volumetric
clouds (towers, storms, flying through them) were planned for NoorSky phase C.

## Options
1. **Improve the flat shading** (normal-mapped layers, parallax). Cheap, but still no sides at the horizon.
2. **A volumetric layer**: keep the layer model (altitude, coverage, density, scale, wind, seed) and give a layer a
   thickness; the coverage map becomes the weather map of a ray-marched slab. Real sides, self-shadowing and
   stacking near the horizon, within today's settings.
3. **Full phase C clouds** (several cloud types, 3D weather textures, temporal reprojection, flying through). The
   most capable and by far the most work; it would delay 8A's review.

## Decision
Option 2, as a choice per layer: `"cloud_type": "layer"` (the default, unchanged) or `"volume"`, with
`"cloud_thickness"` in metres (and the same with the `cloud2_` prefix).
- **Shape.** The layer's 2D coverage (part 4's `cloud_cover_at`, unchanged) is the weather map. Inside the slab a
  height profile gives flat bases and rounded tops, and cloudier places grow taller. Round lumps are carved out
  of it by inverted cellular (Worley) noise, three octaves, so cumulus get their cauliflower look; a three-octave 3D
  value noise erodes the edges finer still. The extinction is scaled so that a vertical column through a full cloud still has the layer's
  `density` as its optical thickness. That is the meaning `density` already has, so cloud shadows and exposure
  metering, which use the column, stay consistent with what is drawn.
- **Light.** Along the view ray, the sun's light at each sample is attenuated by a short march towards the sun
  through the slab. A two-lobe phase function gives the silver lining and the dull far side. Multiple scattering
  is approximated by a few octaves of weaker extinction and a flatter phase (Wrenninge's approximation). The sky
  above is the ambient light, more at the top than at the base. The light at the slab comes from the live sky's
  `cloud_light`, as for flat layers.
- **Reference first.** The C++ reference (`render/clouds.h`) is what the tests check against physics and shape.
  The shaders (`clouds.hlsli`, `clouds_volume.hlsl`) are a port of it.
- **The look was chosen, not guessed.** Rendered variants side by side (lighting: five; lumps: five) on the test
  objects at noon and in the evening and on Hillside, and the pick was made by eye: "B" (darker bellies, a brighter silver
  lining) and "P4" (strong cauliflower lumps), then "F3" for the edges (below). The numbers are named constants
  in both files.
- **Edges.** The first version's edges were one even soft band. The cause was the anti-sparkle fade: the lumps and
  the erosion faded out once a pixel covered a quarter of a noise cell, which at normal viewing distances is always,
  so outlines were just the smooth coverage map. They now fade only when a pixel covers a whole cell
  (`kVolumeDetailFade`); the erosion is finer (`kVolumeDetailScale` 0.12) and turns wispy (inverted noise) towards
  the base; low densities fade further (`kVolumeEdge`), a see-through fringe at every edge. Better, not cured: see
  Unresolved.
- **Speed: bake and halve.** Computing cellular noise at every sample cost 20 ms a frame. The volume noises tile
  (every 4 first-octave cells), so the GPU bakes them once into a 128³ RGBA8 texture (4 MB; `CSNoise`), and the
  march (`CSMarch`) runs as a compute pass at half resolution into an RGBA16F image (radiance, and what is let
  through). The sky pass lays that image over the sky where the flat layers would go. A layer's seed picks where in
  the tile it reads (`cloud_noise_offset`). The reference uses the same tiling noise, so its tests still describe
  what the GPU draws (the GPU reads it filtered, the reference computes it exactly).
- **Determinism before speed.** v1 has no temporal accumulation. Each pixel's march is jittered by a fixed pattern
  of the pixel only, so a frame depends on nothing but the scene and the time: goldens and built-game comparisons
  keep working. Temporal reprojection (with a deterministic test mode) is left for phase C.
- **Not changed:** flat layers, their look and defaults (every existing golden must stay identical); cloud shadows
  (they use the column optical depth, the same for both styles); the live sky's reflections and irradiance (they
  see the clouds as the flat approximation of the same coverage).

## Consequences
- **Cost, measured:** a volume layer adds 1.50 ms (1.51 with the F3 edges) to the fastest frame at 1280×720 on an RTX 3080 (1.81 ms against
  0.31 ms for the same objects without clouds; flat layers add 0.04 ms). The way there: 10.8 ms in the sky pass,
  5.3 ms once the march stopped being compiled in twice, 20.4 ms with the cellular lumps computed live, 1.8 ms
  baked and at half resolution. `render.clouds_volume_cost` fails above 2.5 ms (room for a shared desktop GPU).
- **Tested:** the reference (`clouds:` unit tests: flat bases and rounded tops; a column keeps the layer's density;
  a level look from cloud height meets cloud where a flat layer has none; near the horizon the volume covers 79% of
  the sky against the flat layer's 28%; cloud near the top is brighter than near the base, per unit of cloud; thick
  clouds are darker from below; the noises tile); golden `Atmosphere_Volume`; `cli.environment_builds_identically`
  now has a volume layer (a built game draws it like the project); every older golden unchanged; Vulkan validation
  clean. Flat layers remain the cheap option and look exactly as before.
- A volume is seen from below and from the side, not from above or from inside: no flying through it yet (phase C).
- Reflections and ambient light don't see the volume's shape, only its coverage. That is fine for distant clouds,
  and noted for phase C.

## Unresolved
- Whether any preset switches to volumetric clouds. Presets change only after looking at renders.
- **Edges, round two** (the ideas inbox): the carving stretches what's left back to full density, so edges go opaque
  at once; the tile has no mipmaps, so detail fades all at once rather than octave by octave; the half-resolution
  upscale softens every edge alike. Reshape the ramp, mip the tile, maybe an edge-aware upscale.
- The 2D coverage is still computed at every sample (5 octaves): baking it per frame would take the cost under
  1 ms. A layer of each style in the same sky draws the volume where the first volume layer comes in the
  far-to-near order (a flat layer between two volumes isn't interleaved).
- Cloud types beyond cumulus-like (towers, cirrus streaks), and flying through: phase C.
