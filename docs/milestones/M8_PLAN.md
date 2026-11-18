# Milestone 8 plan: animation, FX, water, weather and richer worlds

**Goal (spec):** grow the creative toolset with complete vertical slices.

**Gate (spec):** each system has editor authoring, runtime behaviour, serialization, docs, performance
notes and tests. A feature that merely draws something for a frame is not finished.

**How it's split** (agreed 2026-11-18, from the [proposal](M8_PROPOSAL.md)): four groups, in this order,
each ending in its own review, like Milestone 7 ended in one.

| Group | What | Status |
|---|---|---|
| **8A: Sky, atmosphere and environments** | NoorSky phase A ([brief](../ideas/NOORSKY.md)): a sun-driven sky, aerial perspective and fog, environment lighting rebuilt on the GPU so the sun can move, a layered cloud sky, night (moon, stars), **space** (no atmosphere: stars, planets, a sun that doesn't scatter), a `.noorenv` asset with six presets and a gallery, weather zones that blend environments across space | **Next** (parts below) |
| **8B: Particles and sprites** | A particle system with an editor (rain, snow and dust for 8A's weather, FX, gameplay); sprite sheets and 8-direction billboards (The Uncounted's creatures) with animation playback from scripts | Planned |
| **8C: Skeletal animation** | Skinned meshes and animations from glTF (any rig Blender exports), an animation player, blending, a simple state machine, root motion, an editor preview; **retargeting** through a humanoid bone map with presets for **Rigify** (deform bones), **Mixamo**, the **Unreal mannequin** and Blender's metarig; Alden Wayfarer as the test character | Planned |
| **8D: Water** | Water bodies (lakes, a sea plane), reflections, shorelines that read the terrain, and **underwater** environments (light absorbed with depth, fog, a surface seen from below), built on 8A's environments | Planned |
| Later (8 or 9) | Volumetric fog and clouds (NoorSky phase C), streaming for big worlds | Not scheduled |

Assets for all of it come mainly from **Blender** (plain glTF), with trees, grass and rocks from NoorMD
now and then (converted by `tools/interchange/noormd_engine_export.py` until NoorMD has its NoorEngine
export mode).

## Where 8A starts

- **Environments today** (Milestone 5, part 5): one `environment` entity per scene with an HDRI, or a
  procedural sky made of three colours (zenith, horizon, ground). Both are turned into a prefiltered
  cubemap and spherical-harmonics irradiance **on the CPU**, cached on disk: fine for a fixed sky, too
  slow to redo every frame for a moving sun (not timed yet: part 2 measures it first).
- **Light:** a directional sun in lux with four shadow cascades, exposure in EV100, tonemapping.
- **Missing:** fog of any kind, aerial perspective (distant hills fading into the sky), a sky that
  follows the sun's position, clouds, moon and stars, a reusable environment asset (settings live on
  each scene's entity), presets, any way to blend between environments.

## Decisions

- **ADR-033 Environments and atmosphere** (to propose in part 1): an environment is an asset
  (`.noorenv`) that a scene's `environment` component references (or overrides field by field). Sky
  modes: `atmosphere` (physically based, sun-driven), `hdri` (as today), `gradient` (today's
  procedural) and `space` (no scattering). Everything that depends on the sun is computed on the GPU in
  small lookup tables (the approach published by Sébastien Hillaire for Unreal, 2020: transmittance,
  multiple scattering and sky-view tables), so time of day costs little. Fog and aerial perspective use
  the same atmosphere, so the far ground always meets the sky without a seam (the brief's warning).
- **Environment lighting on the GPU** (part 2): the prefiltered cubemap and the irradiance are rebuilt by
  compute shaders when the sun moves, a few faces per frame, instead of on the CPU. HDRIs keep their CPU
  path and cache (they don't change).
- **Weather zones** reuse the scene's components: a `weather_zone` volume with an environment and a blend
  distance; the camera's position decides the mix (what The Uncounted's zones do).

## Parts (each ends tested, documented and pushed)

| Part | What | Proves |
|---|---|---|
| 1 ✅ | **Atmosphere** (ADR-033): the sky from the sun's direction (Rayleigh, Mie, ozone; transmittance, multiple scattering and sky-view tables on the GPU), the sun's disc, the sun's light coloured by the air it crosses (red at sunset), `sky: "atmosphere"` on today's environment component | A golden render at noon and at sunset from the same scene; the sun's colour at the horizon matches the published reference within a tolerance; the sky costs under 0.3 ms at 1080p (measured) |
| 2 ✅ | **Lighting from the live sky**: the cubemap and irradiance rebuilt on the GPU when the sun moves (spread over frames); `time_of_day` on the environment (sun direction from the hour, latitude optional) | Dragging the time from dawn to dusk relights the scene smoothly at 60 FPS (measured per frame); an object's colour under the GPU result matches the CPU path for the same sky within a tolerance |
| 3 ✅ | **Fog and aerial perspective**: distance and height fog lit by the sun and sky, distant terrain fading into the atmosphere, per environment; applied to opaque and see-through surfaces | Hillside's far hills fade into the sky without a seam (golden); fog density and height change the picture as documented (unit tests of the fog maths) |
| 4 ✅ | **Clouds (layered)**: one or two cloud layers from tileable noise (coverage, density, altitude, wind speed and direction), lit by the sun (bright edges, dark bases at sunset), moving over time; deterministic from a seed | The same seed and time give the same clouds (golden); overcast coverage dims the sun and softens shadows as documented; the clouds cost under 0.5 ms (measured) |
| 5 ✅ | **Night and space**: moon (phase, light), stars (a deterministic star field), exposure for night that keeps silhouettes readable; `sky: "space"`: no scattering, a black sky with stars, **celestial bodies** (planets, moons, rings: textured spheres or discs at a direction and size, lit by the sun), several suns allowed | A night golden where the house and trees read against the sky; a space golden (a planet with a lit and a dark side, a second sun); the Alien World preset's look becomes possible (later) |
| 6 ✅ | **`.noorenv` assets and presets**: environment assets with every setting, scenes referencing them (with overrides), six presets (Clear Noon, Golden Evening, Overcast, Foggy Dawn, Moonlit Night, Stormy Coast), the editor's environment gallery (thumbnails, apply, duplicate, edit, reset) and inspector | Applying a preset changes sky, light, fog and exposure together (smoke test); duplicating a preset never changes the installed one; a custom environment saves, reopens and builds into a game identically (tests) |
| 7 | **Weather zones**: `weather_zone` volumes that blend environments by the camera's position (fog, sky, light, clouds), with a blend distance; script access (set the environment, the time, a zone's weight) | Walking from a clear zone into a foggy one blends smoothly (a built game's camera path, values checked per frame); a scripted time-of-day loop runs at 60 FPS |
| 8 | **Sample + review**: Hillside shown under the six presets (one golden each), a space scene, performance notes (each feature's GPU cost, low and high settings), the 8A review | The 8A gate |

## Part 1 result (2026-11-18)
Done, with one deviation: the atmosphere runs **on the CPU** (`asset::Atmosphere`) as the reference
implementation, tested against physics (hand-worked transmittance, Chapman's air mass, the CIE clear-sky
zenith luminance, ozone's blue twilight). The GPU tables move to part 2, where the moving sun needs them:
a sun change rebuilds sky and lighting in about 120 ms on the CPU today. Goldens `Atmosphere_Noon` and
`Atmosphere_Sunset`. The sky pass costs at most about 0.2 ms at 1080p (scaled from 0.09 ms for the whole
scene pass at 720p; the player can't render at 1080p yet).

## Part 2 result (2026-11-18)
Done. The atmosphere, the sky image and the environment's lighting (cube, irradiance) are built by compute
shaders, a port of the CPU code: the GPU path reproduces part 1's goldens (mean difference 0.02 and 0.04 of
255). A sun change costs 0.16 ms of GPU time, so the whole rebuild runs in the frame the sun moves;
**spreading it over frames wasn't needed** (risk 2, settled by measuring). `time_of_day` (with latitude, day
of the year, north) places the sun; dragging it from dawn to dusk rebuilds every frame at 60 FPS
(`render.day_cycle`). One lesson: a shared copy of the sky made each frame wait for the previous frame's
presentation (half the frame rate); one copy per frame in flight fixed it.

## Part 3 result (2026-11-18)
Done. Aerial perspective from the atmosphere (a 32³ volume built every frame, 0.025 ms) and height fog
(closed-form optical depth, lit by the sun and the sky), on opaque and see-through surfaces and the sky
itself, so the far ground meets the sky without a seam: golden `Hillside_Haze` (a second scene of the
sample, written by `tools/samples/add_hillside_skies.py`). Fog maths unit-tested against numerical
integration. One fix on the way: two scenes sharing a CSG model are now cooked once, and a model whose
brushes differ between scenes under one entity id is refused instead of silently overwritten.

## Part 4 result (2026-11-18)
Done. Two thin cloud layers from deterministic noise, drifting with game time, lit by the sun and sky at their
altitude (bright edges, darker bodies, red at sunset), drawn sharp in the sky pass, lighting the scene through
the live sky (overcast lights it grey), shading the sun on the ground (drifting cloud shadows; overcast all but
removes direct light). Goldens `Atmosphere_Clouds` and `Atmosphere_Overcast`; cloud maths unit-tested
(determinism, coverage, wind, dimming). Cost: 26-28 µs a frame (budget 0.5 ms). Lighting follows drifting
clouds in steps of 1% of their size, not every frame. The GPU timing tests now judge each run's fastest frame:
on a working desktop the GPU idles in a low power state and medians swing 3-10x.

## Part 5 result (2026-11-18)
Done. The moon (time and phase, its light from a Lambert sphere: 0.16 lux full, the shadow light when it outshines
the sun, the moonlit sky in the scene's lighting), a deterministic star field with the real sky's counts turning
about the pole, `sky: "space"`, celestial bodies with rings and the planet's shadow on them, extra suns, and
automatic exposure decided by measuring (an incident meter with a night bias; risk 3 settled). Goldens
`Hillside_Night` and `Space`. Not done: textured bodies (they're coloured spheres), eye adaptation.

## Part 6 result (2026-11-18)
Done. `.noorenv` environment assets (every setting, exposure included), scenes pointing at one with overrides, nine
built-in presets (the plan's six, tuned from rendered variants, plus Fair Weather, Dusk and Pink Mist where two variants
were worth keeping) that are never written, the editor's Environments gallery (pictures, Apply, Duplicate, Edit, Reset)
and the editor API v24. Proven: applying a preset changes sky, sun, fog and exposure together in one undo step (API test
on what a game draws, and the editor smoke test); duplicates and edits never touch the installed preset; an edited
environment saves, reopens and builds into a game identically (pixel-identical, the same metered EV). Corrected on
the way: a cloud's diffuse light now depends on its thickness, in its shading and in automatic exposure. Not done:
pictures for a project's own environments (they show their preset's).

## Highest risks (settle early)
1. **Matching the references honestly.** A physically based sky is easy to make plausible and hard to
   make right. Part 1 compares against published values, not against our taste.
2. ~~**Cost of moving the sun.**~~ **Settled in part 2:** 0.16 ms a rebuild on the GPU, every frame, no
   spreading needed.
3. ~~**Exposure.**~~ **Settled in part 5:** an incident light meter with a night bias, measured over a day
   (EV 14.8 at noon to −3.6 by moonlight); no histogram needed.
4. **Scope creep from the brief.** Phase A only: volumetric clouds, storms with lightning, rain on
   surfaces and seasonal changes are phases B and C.

## Open questions
1. ~~The Uncounted, Chapter Two: sprite characters or 3D?~~ **Answered (2026-11-18): sprites**, like Chapter
   Zero. 8B's sprite sheets and 8-direction billboards are what it needs; 8C stays third.
2. Underwater environments are planned with 8D (they need a water surface); is a "deep sea" scene without
   any surface (a submarine game) wanted earlier? It would fit in 8A part 5 as an environment mode.

## Noted for 8C (from the ideas inbox, 2026-11-18)
- **FBX import** through ufbx (MIT), if its licence check and robustness tests pass; glTF stays the main path.
- **An animation studio in the engine** (our own rig, posing, keys, curves, IK) as a follow-up group after 8C.

## Not in 8A (planned later)
Volumetric clouds and fog, lightning and storms, rain and snow (need 8B's particles), wet surfaces,
seasons, the remaining six presets (phase B: Cold Morning, Autumn Afternoon, Rainy Grey, Desert Haze,
Fantasy Crisp, and Alien World in phase C), environment graphs and Lua drivers.
