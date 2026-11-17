# ADR-012: PBR shading model and render path

**Status:** Accepted for what part 1 built (HDR chain, units, BRDF, tonemapping); the rest stays Proposed until its part lands · **Date:** 2026-11-17 · **Milestone:** 5

## Context
The renderer today (Milestones 1–4) draws vertex colours with one fixed Lambert light straight
into the sRGB swapchain: no textures, no materials, no shadows, no HDR. Milestone 5's gate (spec
29) is "a PBR sample displays predictable materials under different HDRIs; the importer preserves
colour spaces; changes serialize; render tests detect regressions". Spec 10.2–10.4 asks for a
documented BRDF, physically meaningful roughness/metalness, linear lighting, a chosen and
benchmarked render path, HDR with exposure and tonemapping, and warns against claiming several
half-working paths. This ADR fixes the shading model, units, colour pipeline and render path, so
textures (ADR-029) and materials (ADR-030) have something stable to feed.

## Options
1. **Shading model**
   - **A. Metallic-roughness (the glTF 2.0 core model)**: what GLB files, NoorMaterial, Blender,
     Godot and most texture tools produce. Extensions (clearcoat, sheen, transmission) layer on it.
   - **B. Specular-glossiness**: older, deprecated in glTF, fewer sources.
2. **Render path**
   - **A. Forward** (depth pre-pass, then one shading pass; clustered light lists when many lights
     arrive). MSAA works, transparency and foliage shade like everything else, one path for every
     editor viewport, low bandwidth.
   - **B. Deferred** (G-buffer). Cheap many lights, but MSAA and transparency need extra paths,
     G-buffer bandwidth, and material variety is capped by the G-buffer layout.
3. **Light units**
   - **A. Photometric** (sun in lux, point/spot in lumens, sky in nits) with a camera exposure (EV100).
   - **B. Artist units** ("intensity 1"), simpler at first, but HDRIs, sun and lamps never agree.
4. **Tonemapper**: ACES fits (contrasty, shifts hues), **AgX** (Blender's default since 4.0, keeps
   hues in bright colours), **Khronos PBR Neutral** (keeps material colours as authored; for
   material preview), or none (clipping).

## Decision
**1A, 2A, 3A**; **Khronos PBR Neutral** by default, the ACES fit selectable. (The first draft
chose AgX; its curve data comes from Blender's configuration, so it waits for a licence check.
Neutral's reference code is Apache-2.0 and it keeps authored colours, which suits material work.)

- **BRDF** (documented in `docs/rendering/PBR.md` when part 1 lands): Cook-Torrance specular with
  the GGX distribution, height-correlated Smith visibility, Schlick Fresnel (F0 = 0.04 for
  dielectrics, base colour for metals); Lambert diffuse. Roughness is perceptual (squared for
  GGX alpha). Multi-scattering energy compensation is a later improvement, listed, not promised.
- **Colour**: lighting in scene-linear Rec.709/sRGB primaries into a **16-bit float HDR target**;
  exposure (EV100, manual first, auto-exposure later), then tonemap and sRGB encode at the end.
  The HUD overlay is composited after tonemapping (it stays in display space).
- **Path**: forward; a depth pre-pass when shading gets expensive (materials, part 3, measured
  first); 4× MSAA baseline (part 7). Lights: one directional "sun" with
  **cascaded shadow maps** (4 cascades, PCF); a small fixed number of point/spot lights first,
  clustered light lists when a scene needs hundreds (measured, not assumed).
- **Image-based lighting**: an HDRI becomes a cubemap; diffuse from spherical harmonics (SH9);
  specular from a GGX-prefiltered mip chain with the split-sum BRDF lookup table. Computed on the
  GPU when the environment loads and cached in `.noor/cache`.
- **Binding**: bindless textures (Vulkan descriptor indexing: one big array, materials hold
  indices), so one "standard" shader serves every material without a permutation per texture
  combination. Feature permutations (alpha-cutout, skinning later) stay few and named.

## Consequences
- Easier: GLB and NoorMaterial assets look the way their tools showed them; one shader to test;
  MSAA and transparent materials don't need a second path.
- Harder: photometric units need good defaults (exposure presets: "Sunny day", "Overcast",
  "Indoor") or every new scene looks black or white. Hundreds of local lights need the clustered
  step. A real HDR chain means the editor's tint/selection drawing and the HUD move to the right
  places in the frame.
- Must do: a frame structure with named passes (depth, shadows, opaque, sky, transparent,
  tonemap, overlay) and GPU timings per pass in the editor stats (spec 10.1, "passes inspector").
- **Known risk (from ADR-003):** each editor viewport has its own Vulkan device, so textures would
  be uploaded once per viewport. Acceptable for one viewport plus the occasional floating one;
  a shared device is listed in the plan.

## Evidence / benchmark
Part 1 (2026-11-17): `render.golden_Daylight` and `render.golden_Sunset` draw a test scene with
the albedo range from #141414 to #F2F2F2 and saturated colours, and compare it with references
(0.00% difference across repeated runs on the development PC); `render.golden_catches_a_change`
shows the comparison fails on a different picture. Default exposure was chosen by looking:
EV 15 ("sunny 16") looked dim with typical game albedos, EV 14 looks like daylight. Pass
timings are measured (`FrameStats::passes`); the sample-scene numbers come in part 7.

Implementation notes (part 1): the ambient term has a specular part (the sky in the reflection
direction with an analytic environment-BRDF fit). Without it, dielectrics had no 4% sky
reflection and PBR Neutral's toe crushed shadowed dark surfaces to black. Until environments
(part 5), a directional `light` carries its sky's colour and luminance.

## Migration or reversal strategy
Forward → deferred would keep the material and texture formats (they describe surfaces, not
passes); only the renderer's passes change. Units and tonemapper are settings with defaults.

## Reviewed by
Sage (proposal). Tom: plan review.
