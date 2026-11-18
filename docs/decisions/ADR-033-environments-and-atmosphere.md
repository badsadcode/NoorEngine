# ADR-033: Environments and atmosphere

**Status:** Proposed (Milestone 8A; parts 1-5 implemented: the atmosphere sky on the GPU, time of day, fog, aerial perspective, clouds, night and space) · **Date:** 2026-11-18 · **Milestone:** 8

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
- **Part 2 moves it to the GPU** (`render::Renderer::set_atmosphere`, `shaders/atmosphere.hlsl`, the
  renderer's first compute shaders): a straight port of the CPU code (same tables, same march, same
  pyramid, GGX prefiltering and SH9), so the CPU path stays the reference (`noor_player --cpu-sky`).
  Tables rebuild when the settings change; the sky image, its pyramid, the cube's seven mips and the
  irradiance when the sun moves, all in that frame, before the shadow pass. The irradiance moved from
  the frame uniforms into a small storage buffer (frame set binding 3) so the GPU can write it without
  a read-back.
- **One copy per frame in flight** of what the scene reads (sky, cube, irradiance): a frame rebuilds its
  own copy while the previous frame may still draw with the other. The first version shared one copy;
  its barrier on earlier fragment work made every frame's compute wait for the previous frame's
  presentation (that work sits behind the swapchain's acquire), which halved the frame rate.
- **Aerial perspective** (part 3): a 32×32×32 froxel volume over the camera's view (slices spaced by the
  square root of distance out to 32 km of air), built by compute every frame from the same atmosphere
  (`CSAerial`, reference `asset::Atmosphere::aerial_perspective`), sampled by every surface: colour ×
  transmittance + inscatter. `aerial_scale` multiplies scene distances (1 physical; a 256 m world can look
  kilometres deep). Only with the atmosphere sky.
- **Height fog** (part 3, any sky): full density below `fog_height`, thinning exponentially above; its
  optical depth along a ray in closed form (no marching), lit by the sun through a Henyey-Greenstein phase
  and the sky's average radiance (the irradiance's constant term). Applied to opaque and see-through
  surfaces **and to the sky** (to infinity): on the horizon the sky becomes the fog's colour, which is where
  distant fogged ground ends up, so the two meet without a seam. Reference and tests: `noor/render/fog.h`;
  shaders share `fog.hlsli`. No shadows inside the fog (volumetric shadows: phase C).
- **Layered clouds** (part 4, atmosphere sky): up to two thin layers at an altitude, from deterministic value
  noise (seed, coverage, density, feature size), drifting with the wind over game time (`render::Lighting::time`).
  Lit by the sun as it reaches the layer and the sky above it (a compute step from the same tables), with a look
  towards the sun through the cloud's own cover for bright thin edges and darker bodies: an approximation for a
  thin layer, not a volume. Drawn sharp per pixel in the sky pass over a cloud-free background; the environment's
  lighting is built from a cloudy copy (overcast lights the scene grey) and follows drifting clouds in steps of 1%
  of their feature size; the scene's shader dims the sun by the clouds between each point and the sun (drifting
  cloud shadows; overcast all but removes direct light, so shadows fade). Reference: `noor/render/clouds.h`;
  shaders: `clouds.hlsli`. Fine noise fades with a pixel's footprint, so far clouds don't shimmer.
- **Night and space** (part 5): **celestial bodies** (the moon, planets, rings) are Lambert spheres at a direction
  and angular size, lit by the real sun and any extra suns in the sky pass; rings are an annulus that the planet
  shades. The moon is one of them: placed by time and phase (`asset::lunar_direction`), its light on the ground
  follows from the same sphere (`asset::moon_illuminance`: a full moon about 0.16 lux), it becomes the shadow
  light when it outshines the sun, and its glow is added to the live sky (the moonlit sky lights the scene).
  **Stars**: a deterministic field (cells on a cube, real counts by magnitude, colours by temperature) turning
  about the celestial pole, dimmed by the air mass, clouds and fog. **`sky: "space"`**: no air, a black sky,
  stars, bodies seen in every direction. **Extra suns**: directional lights after the first, without shadows.
  **Automatic exposure** (`ev100: "auto"`): an incident light meter (sun, sky, moon, starlight, clouds) with a
  night bias of up to 2.5 stops less exposure, decided by measuring a day (docs/rendering/MEASUREMENTS.md);
  deterministic, no histogram, no adaptation over time (yet). The live sky's images are scaled by
  `live_sky_scale` (larger at night) so half floats keep their precision. References and tests:
  `asset/atmosphere.h` (moon), `render/stars.h`, `render/exposure.h`.
- **Time of day** (`"time_of_day"`, `"latitude"`, `"day_of_year"`, `"north"` on the environment, any sky):
  the sun's direction from a simple solar model (`asset::solar_direction`: declination from the day,
  hour angle from the time; no equation of time or refraction).

## Consequences
- **Checked against physics, not taste** (`tests/unit/test_atmosphere.cpp`): sea-level zenith
  transmittance equals exp(−optical depth) worked out by hand and the published (0.94, 0.86, 0.76);
  the horizon air mass matches Chapman's √(πR/2H) within 5%; the noon zenith's luminance is within 35%
  of the CIE clear-sky formula at 30°, 45° and 60° sun elevation (the model gives about 2 kcd/m² at 60°,
  the CIE formula 2.0 for a very clear sky); the sun at 2° is red; ozone keeps the twilight zenith blue
  (green-to-blue falls by about 30% with it, as Hulburt explained in 1953).
- **Goldens:** `Atmosphere_Noon` and `Atmosphere_Sunset` (the same objects under the sun at 60° and 3°).
- **Cost (CPU, part 1):** the tables take about 0.1 s once per setting; each sun change rebuilds the
  sky and its lighting in about 120 ms.
- **Cost (GPU, part 2):** a sun change costs **0.16 ms** of GPU time at full clocks (0.5–1 ms while the
  GPU is clocked down), the tables a fraction of a millisecond once; dragging the time from 5:00 to
  21:00 rebuilds every frame at 60 FPS (`render.day_cycle`). Spreading the work over several frames,
  which the plan expected to need, wasn't.
- **Night and space** (part 5): goldens `Hillside_Night` (the house and trees against the moonlit sky) and `Space`
  (a ringed planet with a lit and a dark side and its shadow on the rings, two suns). Bodies are untextured yet.
- **Clouds cost** 26-28 µs a frame at 1280×720 (fastest frames, two layers, against the same scene without),
  plus a 0.08 ms lighting rebuild a few times a minute while they drift. Goldens `Atmosphere_Clouds`,
  `Atmosphere_Overcast`.
- **Fog and aerial perspective cost** almost nothing: the aerial volume 0.025 ms a frame on an RTX 3080,
  the fog a handful of exponentials per pixel. Golden `Hillside_Haze`.
- **GPU equals CPU:** the GPU path reproduces part 1's goldens (made by the CPU path) with a mean
  difference of 0.02 (noon) and 0.04 (sunset) out of 255, and no pixel outside the tolerance; the CPU
  path still passes the same goldens (`render.golden_Atmosphere_*_cpu_sky`). The sky pass itself is a texture lookup plus the disc: the whole scene pass,
  sky included, is 0.09 ms at 1280×720 on an RTX 3080 (about 0.2 ms at 1080p by pixel count; the player
  can't render at 1080p yet, so that figure is scaled, not measured).
- The HDR target is half floats, so the sun's disc is clamped at 60,000 (it tonemaps to white anyway).
- Old scenes are unchanged: `sky` is new and optional; the gradient and HDRI paths are untouched.

## Unresolved
- Textured celestial bodies, eclipses, the moon's own shadow on the ground at an eclipse, real lunar calendars.
- Exposure that adapts over time (eye adaptation) as an option on top of the metered value.
- The `.noorenv` asset format and overrides (part 6), and blending two atmospheres (part 7: blend
  parameters, or blend the results?).
- Other planets (Mars-like coefficients) are possible with the same settings; no preset yet.
- Exposure across day and night (part 5).
