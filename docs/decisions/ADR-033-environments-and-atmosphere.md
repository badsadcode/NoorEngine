# ADR-033: Environments and atmosphere

**Status:** Proposed (Milestone 8A; part 1 implemented: the atmosphere sky, on the CPU) · **Date:** 2026-11-18 · **Milestone:** 8

## Context
Milestone 8A ([plan](../milestones/M8_PLAN.md), [NoorSky brief](../ideas/NOORSKY.md)) wants skies that
follow the sun, fog and aerial perspective, clouds, night, space, reusable environment assets with
presets, and weather zones. Today (Milestone 5, part 5) an environment is one `environment` component per
scene: an HDRI, or a "procedural sky" of three colours that knows nothing about the sun. Its lighting
(a prefiltered cubemap and nine spherical-harmonics coefficients) is computed on the CPU, which suits a
sky that never changes.

The brief's warnings: a physically based sky is easy to make plausible and hard to make right; the far
ground must meet the sky without a seam; and the sun's colour, the sky's colour and the light on objects
must agree, or the picture falls apart.

## Options
- **Sky model**
  - **A: Physically based, single atmosphere model** (Bruneton 2008; Hillaire 2020, the one Unreal
    ships): Rayleigh, Mie and ozone layers over a spherical planet; transmittance and multiple-scattering
    lookup tables; the sky by ray marching. Correct sunsets, twilight, altitude and haze from a handful of
    physical parameters; one model for the sky, the sun's colour, fog and aerial perspective.
  - **B: Analytic fits** (Preetham 1999, Hosek-Wilkie 2012). Cheap, but only for an observer on the
    ground, no twilight (sun below the horizon), no altitude, and nothing for fog to share.
  - **C: Artist gradients** (today's three colours, more stops). Full control, no physics: every
    time of day needs its own colours, and the sun's colour never matches the sky.
- **Where it's computed**
  - **A: GPU lookup tables per frame** (Hillaire: transmittance 256×64, multiple scattering 32×32, a
    sky-view table, an aerial-perspective volume). Cheap enough for a moving sun.
  - **B: CPU**, rebuilt when the sun moves. Simple and easy to test; too slow for a moving sun.
- **Environment data**
  - **A: An asset** (`.noorenv`) that scenes reference and override field by field, with presets.
  - **B: Settings on each scene's entity** (today). Can't be shared, previewed or blended.

## Decision (proposed)
**Sky model A, environment data A; computation B now, A from part 2.**
- **Sky modes** on the `environment` component (and later the `.noorenv` asset): `atmosphere`
  (physically based, sun-driven), `hdri` (as today), `gradient` (today's procedural sky) and, in part 5,
  `space` (no scattering). `"sky"` absent means `hdri` with an HDRI, `gradient` without.
- **The atmosphere** (`noor/asset/atmosphere.h`): Earth's coefficients from Hillaire 2020 / Bruneton's
  reference; Rayleigh (exponential, 8 km), Mie (exponential, 1.2 km, Cornette-Shanks phase, g = 0.8,
  scaled by `turbidity`), ozone (a tent 10–40 km, absorption only), ground albedo. A transmittance table
  (256×64, more texels near the horizon and the ground), a multiple-scattering table (32×32, Hillaire's
  isotropic approximation, summed as a geometric series) and a 40-step ray march for the sky. Radiance is
  relative to a sun of illuminance 1 outside the atmosphere.
- **The sun follows the air:** with `sky: "atmosphere"` the sun light's `illuminance` means *outside*
  the atmosphere and its `color` the sun's own (white by default); the scene's sun colour is that times
  the transmittance from the observer to the sun. The sky's intensity is the sun's illuminance, so sky,
  sun and the light on objects always agree. The renderer draws the sun's disc (0.53°, limb-darkened)
  from the same values.
- **Below the horizon** the background stands for distant land: at least 20 km of air in front of the
  ground, so the horizon doesn't end in a hard line. Real geometry gets aerial perspective in part 3,
  from the same atmosphere.
- **Part 1 runs on the CPU** (a deviation from the plan, which said GPU tables from part 1): the CPU
  version is the **reference** the GPU port in part 2 is compared against, and it can be tested against
  physics without a GPU. The sky image (512×256) feeds the existing environment pipeline (prefiltered
  cubemap, irradiance), keyed by the sun's direction.

## Consequences
- **Checked against physics, not taste** (`tests/unit/test_atmosphere.cpp`): sea-level zenith
  transmittance equals exp(−optical depth) worked out by hand and the published (0.94, 0.86, 0.76);
  the horizon air mass matches Chapman's √(πR/2H) within 5%; the noon zenith's luminance is within 35%
  of the CIE clear-sky formula at 30°, 45° and 60° sun elevation (the model gives about 2 kcd/m² at 60°,
  the CIE formula 2.0 for a very clear sky); the sun at 2° is red; ozone keeps the twilight zenith blue
  (green-to-blue falls by about 30% with it, as Hulburt explained in 1953).
- **Goldens:** `Atmosphere_Noon` and `Atmosphere_Sunset` (the same objects under the sun at 60° and 3°).
- **Cost:** the tables take about 0.1 s to build once per setting. **Each sun change rebuilds the sky
  and its lighting in about 120 ms on the CPU**: fine for a fixed sun, not for time of day. That is
  part 2's job (GPU). The sky pass itself is a texture lookup plus the disc: the whole scene pass,
  sky included, is 0.09 ms at 1280×720 on an RTX 3080 (about 0.2 ms at 1080p by pixel count; the player
  can't render at 1080p yet, so that figure is scaled, not measured).
- The HDR target is half floats, so the sun's disc is clamped at 60,000 (it tonemaps to white anyway).
- Old scenes are unchanged: `sky` is new and optional; the gradient and HDRI paths are untouched.

## Unresolved
- The GPU tables (part 2) and how far a sun move may be spread over frames.
- The `.noorenv` asset format and overrides (part 6), and blending two atmospheres (part 7: blend
  parameters, or blend the results?).
- Other planets (Mars-like coefficients) are possible with the same settings; no preset yet.
- Exposure across day and night (part 5).
