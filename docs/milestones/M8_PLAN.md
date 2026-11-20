# Milestone 8 plan: animation, FX, water, weather and richer worlds

**Goal (spec):** grow the creative toolset with complete vertical slices.

**Gate (spec):** each system has editor authoring, runtime behaviour, serialization, docs, performance
notes and tests. A feature that merely draws something for a frame is not finished.

**How it's split** (agreed 2026-11-18, from the [proposal](M8_PROPOSAL.md)): four groups, in this order,
each ending in its own review, like Milestone 7 ended in one.

| Group | What | Status |
|---|---|---|
| **8A: Sky, atmosphere and environments** | NoorSky phase A ([brief](../ideas/NOORSKY.md)): a sun-driven sky, aerial perspective and fog, environment lighting rebuilt on the GPU so the sun can move, a layered cloud sky, night (moon, stars), **space** (no atmosphere: stars, planets, a sun that doesn't scatter), a `.noorenv` asset with six presets and a gallery, weather zones that blend environments across space | **Done** ([review](M8A_REVIEW.md)) |
| **8B: Particles and sprites** | A particle system with an editor (rain, snow and dust for 8A's weather, FX, gameplay); sprite sheets and 8-direction billboards (The Uncounted's creatures) with animation playback from scripts | **Done** ([review](M8B_REVIEW.md)) |
| **8C: Skeletal animation** | Skinned meshes and animations from glTF (any rig Blender exports), an animation player, blending, a simple state machine, root motion, an editor preview; **retargeting** through a humanoid bone map with presets for **Rigify** (deform bones), **Mixamo**, the **Unreal mannequin** and Blender's metarig; Alden Wayfarer as the test character | **Next** ([parts below](#8c-skeletal-animation), proposed) |
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
| 7 ✅ | **Weather zones**: `weather_zone` volumes that blend environments by the camera's position (fog, sky, light, clouds), with a blend distance; script access (set the environment, the time, a zone's weight) | Walking from a clear zone into a foggy one blends smoothly (a built game's camera path, values checked per frame); a scripted time-of-day loop runs at 60 FPS |
| 7b ✅ | **Clouds with depth** (added 2026-11-19, [ADR-034](../decisions/ADR-034-volumetric-cloud-layers.md)): a layer can be a **volume** (`"cloud_type": "volume"`, a thickness): the layer's coverage map extruded into a slab with flat bases and rounded tops, eroded by 3D noise, ray-marched with light marched towards the sun. The flat layers stay exactly as they are, as a style of their own | A cloud seen edge-on has sides (a horizontal ray through the slab hits it; through a flat layer, nothing); thick clouds are dark underneath and sunlit sides bright (CPU reference tests); every existing golden unchanged; a volumetric golden; the cost measured (target: under 1.5 ms at 1280×720); a built game draws it identically |
| 8 ✅ | **Sample + review**: Hillside shown under the six presets (one golden each), a space scene, performance notes (each feature's GPU cost, low and high settings), the 8A review | The 8A gate |

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

## Part 7 result (2026-11-19)
Done. `weather_zone` components (box or sphere on their entity, a blend distance, a priority, an environment that is
a project `.noorenv` or settings of its own, an optional script-set weight) blend over the scene's environment by
the scene camera's position (`asset::blend_environments`: numbers linear, periodic fields the short way, colours in
linear light, switches and seeds at the halfway point). Scripts drive zones, the environment and the time through
the usual `entity:get`/`entity:set`. Editor: Create ▸ Weather zone, and an environment picker for zones in the
Inspector. Proven: `cli.weather_walk` walks a built game's camera into a fog bank for 540 frames (fog monotone, at
most 1.2% of the way per frame, exposure steps of 0.01 EV at most, the zone fully on at the end, the built game
identical to the project frame by frame, gathering the scene under 8 ms for 95% of frames and never as long as an atmosphere build; it measured 0.5 ms
median, 1 ms worst, frames 16.66 ms); a unit test rolls a storm in and runs the clock from Luau scripts; the scripted time-of-day
loop's 60 FPS is `render.day_cycle`'s. Fixed on the way: blending rebuilt the CPU atmosphere's tables every frame
(20 FPS), now answered from a prewarmed grid of atmospheres (`Atmosphere::Bracket`); and a zone fully on replaced the
whole environment instead of what it mentions. Not done: the editor's viewport blends by the scene camera, not the
fly camera.

## Part 7b result (2026-11-19)
Done. A cloud layer can be a volume (`"cloud_type": "volume"`, `"cloud_thickness"` in metres; ADR-034): the layer's
coverage map extruded into a slab with flat bases and rounded tops, carved into cauliflower lumps by cellular noise,
eroded by value noise, and lit by a march towards the sun (bright tops and silver linings, grey bellies). The look
was picked by eye from rendered variants (lighting "B", lumps "P4", edges "F3"). The flat layers are untouched, as a style of
their own. Proven: the reference's shape and physics tests (sides where a flat layer has none; 79% against 28% of the
sky covered near the horizon; tops brighter per unit of cloud; thick clouds darker from below; the noises tile);
golden `Atmosphere_Volume`; a built game draws a volume identically (`cli.environment_builds_identically`); every
older golden unchanged; validation clean. Cost: **1.50 ms** at 1280×720 on an RTX 3080 (the target was 1.5), after
baking the noises into a 3D texture and marching at half resolution (from 20.4 ms computed live). Not done: no
preset uses volumes yet (to be decided from renders); edges are better but still too even (round two is in the ideas
inbox); the coverage map is still computed per sample.

## Part 8 result (2026-11-19)
Done: the 8A gate passes ([review](M8A_REVIEW.md)). Hillside under all nine presets (applied through the editor API by
`tools/samples/add_hillside_presets.py`) and on an airless world with a ringed planet and two suns, ten goldens;
each sky's cost measured on the sample (`tools/samples/measure_hillside_skies.py`: a whole frame 1.1-2.0 ms of GPU
at 1280x720, volumetric clouds +0.7 ms). Exception: the plan's "low and high settings" don't exist yet (no quality
settings in the engine).

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

---

# 8B: particles and sprites

**Status:** proposed (2026-11-19), waiting for a go.

## Where 8B starts

- **Nothing moves by itself yet** but rigid bodies and scripts: no particles of any kind, no sprites. The HUD
  draws textured quads in screen space only.
- **What the renderer already has:** instanced batches (ADR-032: one draw for many copies, culled on the GPU's
  behalf by the CPU), a see-through pipeline sorted back to front, flipbook-ready texture arrays in the bindless
  table, the live sky's light and fog for lighting anything in the air, cloud wind (8A) to blow it about.
- **Who's waiting:** 8A's weather (rain and snow need particles: "Not in 8A"), and The Uncounted, Chapter Two,
  whose creatures are 8-direction billboard sprites with idle/walk/run/attack/hurt/death animations, and whose
  first chapter uses particles everywhere.
- **The spec (17.4 NoorFX)** asks for much more than one group can honestly deliver: emitters, spawn shapes,
  curves, collisions, force fields, trails, ribbons, mesh particles, GPU simulation where viable, events,
  parameter binding, and an editor with a reproducible seed, solo/mute and profiling. 8B takes what the weather
  and The Uncounted need, built so the rest can follow without a rewrite.

## Decisions (ADR-035, to propose with part 1)

- **Two kinds of particles, because they have different jobs.**
  - **Effect particles** (fire, smoke, sparks, dust puffs, debris, magic): an `emitter` component, simulated
    on the CPU at a fixed 60 Hz step from a seed, so the same scene always gives the same particles (goldens,
    built games, replays). A few thousand per effect is the scale; CPU first, like the atmosphere's
    reference in 8A, and moved to the GPU only if measuring says so.
  - **Weather particles** (rain, snow, drifting dust): no simulation at all. Each drop's position is a pure
    function of its index, the time, the wind and the seed, worked out in the vertex shader inside a box that
    follows the camera and wraps. Hundreds of thousands cost almost nothing, need no memory per drop, and are
    deterministic by construction. Driven by the environment (`precipitation`, intensity), so weather zones
    blend rain in like they blend fog.
- **Drawn as instanced billboards** through the existing batching: camera-facing quads (or stretched along
  their velocity for rain and sparks), flipbook frames from a texture, blend modes alpha / additive /
  premultiplied, lit by the sun and the live sky's ambient and fogged like everything else. Sorted per emitter
  back to front.
- **Sprites are a component, sheets are an asset.** A `.noorsprite` file: a texture, a frame grid (slicing),
  named animations (frames, rate, loop) and an optional 8-direction layout. A `sprite` component draws one as
  a billboard (upright for characters, fully camera-facing for effects), alpha-cutout so it writes depth and
  can cast a shadow; nearest or linear filtering (pixel art). The direction row is picked from the angle
  between the entity's forward and the camera, so a creature walking away shows its back.
- **Scripts drive both:** `entity:play("walk")`, speed, events when an animation ends; `emitter:burst(n)`,
  start/stop, parameters (Lua and C#, the same API).
- **Authoring:** an Effects panel (a stack of modules: spawn, shape, velocity, forces, size/colour over life,
  render) with live preview in the viewport and presets; a sprite sheet slicer with animation preview.
  A node graph for effects (the spec's "effects graph") is later: the module stack covers 8B's effects.

## Parts (each ends tested, documented and pushed)

| Part | What | Proves |
|---|---|---|
| 1 ✅ | **Particle core** (ADR-035): the `emitter` component and `.noorfx` asset, spawn rates and bursts, shapes (point, sphere, box, cone), lifetime, velocity, gravity, drag, wind; curves over life (size, colour, alpha); the deterministic fixed-step CPU simulation | The same seed gives the same particles to the bit (Debug = Release, project = built game); rates, lifetimes and forces match their maths (unit tests); 10,000 particles simulate in under 1 ms (measured) |
| 2 ✅ | **Drawing particles:** instanced billboards (facing, velocity-stretched), flipbooks, blend modes, lit and fogged, sorted | Goldens of a campfire (fire, smoke, sparks) at noon and at night; correct order through the see-through pass; GPU cost measured |
| 3 ✅ | **Weather particles:** rain, snow and dust from the environment (`precipitation`), a camera-following wrapped volume worked out in the vertex shader, blown by the clouds' wind, blended by weather zones | Walking into a weather zone starts the rain smoothly (the 8A walk, extended); 200,000 drops under 0.5 ms (measured); a rain golden and a snow golden on Hillside |
| 4 ✅ | **The Effects panel:** module stack, curves, presets (fire, smoke, sparks, dust, rain splash, magic), live preview, seed, solo/mute | Editing a preset changes the preview at once and saves to `.noorfx` identically (smoke test); presets never change when edited copies do |
| 5 ✅ | **Sprites:** the `.noorsprite` asset and slicer, the `sprite` component (billboard modes, cutout, shadows, filtering), flipbook animation | A sliced sheet's frames match their grid exactly (unit test); goldens of sprites in light and in shadow; an editor smoke test slices a sheet and previews an animation |
| 6 ✅ | **8-direction characters and scripts:** direction rows from the camera angle, `play`/speed/events from Lua and C#, emitters from scripts (bursts, parameters) | A sprite walking a circle shows each of the 8 directions in turn (frame indices checked per frame); a script plays walk, then attack, and gets the end event; the same in a built game |
| 7 ✅ | **Gameplay particles:** collisions (ground, terrain, physics planes), sub-emitters on events (a spark hits the ground, a splash), budgets (a cap per emitter and per scene, distance culling) | Particles stop on the terrain, not through it (unit and golden); sub-emitters fire on hits deterministically; a 50-emitter scene stays within its budget (measured) |
| 8 ✅ | **Sample + review:** a stormy evening on Hillside (rain, a weather zone, a campfire with smoke and sparks, a sprite creature walking about), a particle-heavy benchmark scene, performance notes, the 8B review | The 8B gate |

## Part 1 result (2026-11-19)
Done. A new module, `engine/fx` (NoorFX): the `emitter` component and `.noorfx` assets (importer `fx`, overrides like
environments), rate and bursts (intervals, cycles, looping durations), point/sphere/box/cone shapes (volume or
surface), lifetimes and speeds as ranges, gravity, drag towards the wind (the environment's new `wind_speed` and
`wind_direction`), size/colour/alpha over life, world or local space, a cap per emitter; a runtime system stepping
them at the fixed 60 Hz, following component changes. Proven: the maths (`particles:` unit tests: ballistic flight,
terminal velocity, wind, shapes, bursts, caps); determinism (a pinned fingerprint, the same in Debug and Release;
`cli.particles_build_identically`: a campfire replays bit for bit in a built game); cost (10,000 particles: 0.022 ms
a step, budget 1 ms). Fixed on the way: weather zones blended the clouds' wind direction the long way round (since
8A part 7). Not done: particles aren't drawn yet (part 2); no editor panel (part 4: the component is edited as fields).

## Part 2 result (2026-11-19)
Done. Particles are drawn in games: `particle.hlsl` (quads from the vertex index, facing the camera or stretched along
their velocity, flipbooks, a soft dot without a texture), one instanced draw per emitter, merged with see-through
meshes by distance in the see-through pass; alpha, additive and premultiplied blending with one pipeline; lit by the
sun (its shadows and the clouds') and the sky, fogged, with emission. The shared frame shader code moved to
`frame.hlsli` (every golden unchanged). Proven: goldens of a campfire at noon and at night and of smoke between two
glass panes (the order); a built game still replays the campfire bit for bit; cost 0.005 ms for the campfire and
0.58 ms for 10,000 big smoke particles (`render.particles_cost`). Found: automatic exposure doesn't see local light,
so glowing particles suited to noon blow out at night (ideas inbox). Not done: the editor doesn't draw particles yet
(part 4's live preview); textured flipbooks are supported but not yet in a golden (no effect textures in the repo).

## Part 3 result (2026-11-19)
Done. Rain, snow and dust from the environment (`rain`, `snow`, `dust`, 0..1, blown by `wind_speed`/`wind_direction`):
`weather.hlsl` works every drop out from its number, the time and the wind in a box round the camera, with no data
per drop and no simulation; up to 200,000 raindrops, 60,000 snowflakes, 20,000 motes. Proven: the 8A walk into a fog
bank now also rains, and the rain starts smoothly (0 to 1, never back, at most 1.25% a frame) identically in a built
game; 200,000 drops cost 0.18 ms (budget 0.5); goldens of Hillside in rain and in snow. Not done: drops don't stop at
roofs, snow doesn't settle; rain is drawn after see-through things (not tinted by a window in front of it).

## Part 4 result (2026-11-19)
Done. Six built-in effect presets (Fire, Smoke, Sparks, Dust, Rain splash, Magic), never written; editor API v25
(`noor_effect_presets`, `noor_doc_effect`); the Effects panel (a gallery like the Environments one, and a module stack:
spawn, shape, life and speed, forces, over life with curve and gradient editors, render; saved as you edit); a live
preview in the editor's viewport (its own 60 Hz simulation of the scene's emitters, solo, mute, restart, pause, a seed
to roll). `fx::EmitterSet` and `game::particle_draws` are now shared by games and the editor. Proven: API tests (presets
run, apply in one undo step, edits reach what a game simulates and never the preset, reset, duplicate, the preview's
controls); the editor smoke test (the preview shows a placed fire, an edit thins it at once and lands in the file, the
preset unchanged, Mute, undo). Not done: the spec's effects graph and timeline (later, as planned); the preview is the
editor's own simulation, not the game's.

## Part 5 result (2026-11-19)
Done. `.noorsprite` sheets (importer `sprite`; a grid by columns and rows or by frame size, margin, spacing, pivot,
pixels per metre, nearest or linear filtering, named animations as ranges or lists, looping or holding) and the
`sprite` component (sheet, animation, speed, time offset, frame, `upright`/`camera`/`none` billboards, flip, colour,
shadows). A sprite is an ordinary mesh draw: a shared quad with a cutout material per frame, so it's lit, casts and
receives shadows and is fogged with nothing new in the frame; materials gained nearest filtering for pixel art.
`game::SpriteLibrary` is shared by games and the editor's viewport (sprites turn to the editor camera, animate, select,
frame). Editor API v26 (`noor_doc_sprite`) and a sheet editor tab (the sheet with numbered frames, grid and world
settings, animations, an animated preview, Place in scene; a texture's Make sprite sheet). Proven: `sprites:` unit
tests (frames exactly on the grid, margins and spacing, animation timing, the file); `sprite api` tests (create, slice,
save, apply in one undo step); goldens `Sprites_Sun` (each walk frame, tinted, flipped, a camera-facing coin, an
animated walker, all casting shadows) and `Sprites_Shade` (in sun, half in a wall's shadow, in shade); the editor smoke
test (slice 4 x 2 into eight exact frames, the preview steps through the walk, the file holds it, Place in scene, the
viewport draws it, undo). The test sheet is drawn pixel by pixel by `tools/samples/make_test_sprites.py`. Fixed on the
way: mirroring by a negative scale turned the card's lit side away (it mirrors the texture now). Not done: 8 directions
and script control (part 6); clicking a sprite selects its whole card, transparent corners too (not covered by a test);
the editor animates on the viewport's clock, also in Play mode.

## Part 6 result (2026-11-20)
Done. Directional sprite sheets (`"directions": 8`: views in the columns as in The Uncounted, 0 facing the viewer and
turning to the viewer's left; animations name the first column's frames; `"directional": false` for poses that don't
turn), the view picked from the angle between the entity's −Z and the camera. Scripts play, stop and re-speed sprite
animations and hear when a non-looping one ends (Lua `play_animation`/`on_animation_end`, C# `PlayAnimation`/
`OnAnimationEnd`; `noor_rt_api` v4), and burst emitters (`burst`); emitter settings change through the component and
the particles carry on. Coth, from The Uncounted, is the 8-direction test character (Tom's art, published with Tom's
permission). Proven: unit tests (the directions from all sides; a walk round a circle checked frame by frame,
every direction in turn, one step at a time; a Lua script plays walk, then attack, gets the end event and falls down;
speed changes without a jump; stop freezes; a script burst spawns exactly 25 particles; the C# ABI carries the event
and its name); `cli.sprite_scripts_build_identically` (Coth walks, attacks and falls at 2.1667 s in the project and in
a built game, from Lua, and from C# in the built game); golden `Sprites_Coth` (eight Coths in a ring, every view);
the editor smoke test (directions in the sheet editor, the preview turned). Also: the viewport animates on the game's
clock in Play mode. Not done: events in the middle of an animation (footsteps); mirrored half-sets of views.

## Part 7 result (2026-11-20)
Done. Collisions (`collide`: the scene's terrains through a ground query on the same surface as the mesh and physics,
up to 8 planes, bounce, friction, radius, die on hit, a hit speed), sub-emitters (`sub_emitters`: on hit or death,
count, inherited velocity, inline or an effect asset; children kept and drawn with their entity), and budgets (each
emitter's cap; the scene's 100,000, shared out in scene order; drawing-only `render.max_distance`). The Effects panel
gained Collisions and Sub-emitters modules; the editor's preview collides with the snapshot's terrains. Proven: unit
tests (particles dropped on a floor rest on it, a bounce of 0.5 comes back a quarter as high, a sloped plane is never
crossed; thousands of drops land on hilly terrain and none end below it, while without `collide` they fall through;
10 drops landing make exactly 30 splash particles; runs with sub-emitters are identical; 50 emitters asking for
150,000 hold at a 20,000 budget the same way every run); golden `Particles_Terrain` (gravel settling on hills, drops
splashing); `render.particles_cost` (50 fountains: 98,750 of a 100,000 budget, 105,000 held back, CPU 1.0 ms, GPU
0.18 ms); the editor smoke test (a death sub-emitter appears in the preview and is muted with its fire). Not done:
collisions with physics bodies (boxes, models); sub-emitters of sub-emitters; a per-scene budget setting (the 100,000
is fixed for now); the editor's preview has no scene budget.

## Part 8 result (2026-11-20)
Done. The 8B sample: `Hillside_Storm` (tools/samples/add_hillside_storm.py, through the editor API): a storm rolling
into the Dusk valley (a weather zone faded in by a script: rain, wind, low cloud, fog), a campfire from the Fire,
Smoke and Sparks presets (the sparks landing on the hillside), and Coth walking round it on a character controller;
a golden. Performance notes from `tools/samples/measure_hillside_storm.py`, and a particle-heavy benchmark on the
sample (twenty campfires). The sample found that effect particles ignored weather zones' wind and that the particle
system read the environment file every step: both fixed (`game::effect_wind`, tested). The review:
[M8B_REVIEW.md](M8B_REVIEW.md).

## Open questions

1. ~~**The Uncounted's sprite sheet**~~ **Settled in part 6:** Coth's sheet (`friend_coth_painted.png`) is in the
   test data, published with Tom's permission. Earlier wording: **The Uncounted's sprite sheet** (the bearded man: 8 directions × idle / walk / run / attack / hurt, 4 death
   poses), offered on 2026-11-17: where's the file, and may it be committed as a test fixture and in the
   sample, or only used locally?
2. ~~**Pixel art:**~~ **Decided in part 5 (Tom may overrule):** sheets default to nearest filtering (crisp
   pixels, The Uncounted's look); `"filter": "linear"` per sheet for painted sprites.
3. **Effects authoring:** is a module stack enough for 8B, with the node graph later (recommended), or is the
   graph wanted now?

## Not in 8B (planned later)
Trails and ribbons, mesh particles, GPU-simulated effect particles (until measuring asks for them), force-field
volumes, lit/soft particles against scene depth (soft edges where smoke meets the ground), tilemaps and the 2D
workflow (spec section 22), an effects node graph, and lightning (NoorSky phase B).

---

# 8C: skeletal animation

## Where 8C starts

- **Characters can't move their limbs.** Models are rigid: a placed GLB becomes node entities with meshes, and
  the glTF importer (cgltf) reads meshes, materials and collision nodes but ignores skins (joints, weights,
  inverse bind matrices) and animations. Vertices have position, normal, colour, UV and tangent: no joint
  indices or weights. Characters today are capsules (Hillside's walker) or sprites (Coth).
- **What already helps:** the character controller (feet at the origin, slopes, steps), scripts in Lua and C#
  with events (8B's `on_animation_end` pattern), the deterministic runtime (fixed steps, built games replaying
  bit for bit), instanced drawing (ADR-032), the editor's viewport and panels, and a test culture of goldens
  and built-game comparisons.
- **Who's waiting:** Alden Wayfarer (the mascot, a 3D character); Tom's Blender characters (Rigify rigs, plain
  glTF); first- and third-person templates (spec 18.5: "working skeletal/animation integration"); Mixamo-style
  animation sets that people bring.
- **The spec (18.1-18.3)** asks for bones, skin weights, clips, blend trees, state machines, transitions,
  masks, additive blending, root motion, retargeting, editor previews with timelines, and in 18.2 procedural
  animation (IK, look-at, foot placement, ragdolls). 8C takes the core that every 3D character needs and that
  the plan named (player, blending, a simple state machine, root motion, preview, retargeting), built so IK and
  the rest can follow without a rewrite.

## Decisions (ADR-036, to propose with part 1)

- **glTF first, any rig Blender exports.** Skins and clips come from the GLB itself (Blender's exporter, Rigify
  with "Deform bones only", Mixamo through Blender). FBX stays a research item (below), not a dependency.
- **The skeleton is not entities.** A skinned model's bones live in its runtime pose, not as scene entities:
  60 bones times 100 characters would be thousands of entities rewritten every frame, and saved scenes would
  fill with bone transforms. Props attach through **sockets** (a child entity that follows a named bone), and
  scripts can read a bone's world transform.
- **Sampled on the CPU, skinned on the GPU.** Clips are sampled on the CPU from game time (deterministic: the
  same time gives the same pose, in Debug and Release, in the editor and built games), blended there, and turned
  into a joint palette per character. The vertex shader does linear blend skinning (4 influences, the glTF
  limit) from a per-frame storage buffer of palettes; shadows use the same path. Dual-quaternion skinning and
  GPU sampling only if measuring asks for them.
- **An `animator` component and an animation controller asset.** The component plays a clip or runs a controller
  (`.nooranim`: parameters, states with a clip or a 1D/2D blend space, transitions with conditions and
  cross-fade times, layers with bone masks, additive layers). Scripts set parameters and play, cross-fade and
  hear events (`on_animation_event(name)`, end events as in 8B), Lua and C# alike.
- **Root motion is opt-in per clip**: the root's horizontal movement and turn are taken out of the pose and given
  to the entity, through the character controller when there is one, so feet don't slide and walls stop walks.
- **Retargeting through a humanoid bone map** (about 20 core bones: hips, spine, chest, neck, head, shoulders,
  arms, hands, legs, feet, toes; fingers optional), with presets that recognise **Rigify** (`DEF-` bones),
  **Mixamo** (`mixamorig:`), the **Unreal mannequin** and Blender's **metarig**, and a manual override. A clip
  made for one humanoid plays on another by transferring each mapped bone's rotation relative to its rest pose,
  with the hips' height scaled by leg length. T-pose and A-pose rigs are both handled by comparing rest poses.

## Parts (each ends tested, documented and pushed)

| Part | What | Proves |
|---|---|---|
| 1 ✅ | **Skinned meshes:** skins from glTF (joints, inverse bind matrices, `JOINTS_0`/`WEIGHTS_0`), the skinned vertex path (GPU skinning, shadows), the bind pose and a posed skeleton drawn; import warnings (more than 4 influences, unnormalised weights, scaled bones) | Skinning against a CPU reference to the last bit of a vertex (unit); goldens of a character in bind pose and posed, with its shadow; 100 skinned characters measured |
| 2 | **Clips and the player:** animations from glTF (translation, rotation, scale; step, linear and cubic-spline), the deterministic sampler, the `animator` component (clip, loop, speed, time offset), scripts (play, stop, speed, events, end) | Sampled poses equal glTF's own keys and interpolation (unit); a golden at fixed times; a built game samples identically; Lua and C# play a clip and hear its end |
| 3 | **Blending and the controller:** cross-fades, 1D and 2D blend spaces, layers with bone masks, additive layers, the `.nooranim` controller (parameters, states, transitions with conditions) | Blend maths (quaternion sign, normalisation, weights summing to 1) in unit tests; transition timing exact to the frame; an idle-walk-run blend golden sequence |
| 4 | **Root motion and characters:** root motion into the character controller, sockets (props on bones), bone transforms for scripts; Create ▸ Player gets an animated character (spec 18.5) | A walk with root motion covers its clip's distance (within a centimetre) and stops at a wall; a sword in a hand follows it in a golden; the same in a built game |
| 5 | **Retargeting:** the humanoid bone map, the four presets and auto-detection, manual mapping, rest-pose-relative transfer, hips scaling | One clip on rigs of different proportions and rest poses (T and A): bone directions within a few degrees at key frames (unit); a side-by-side golden |
| 6 | **The Animation panel:** clips with a scrubbable timeline, playback speed and looping, the skeleton shown in the viewport, the controller's states and transitions edited, the bone map with its detected preset and warnings | An editor smoke test scrubs a clip, edits a transition and sees the preview follow, and maps a rig |
| 7 | **FBX, research first:** ufbx (MIT) checked against Blender, Mixamo and Unreal FBX files; if it holds up, an importer into the same skeletons and clips as glTF; if not, written down why | A go/no-go note with the files tried; with a go, an FBX and its glTF export import to the same skeleton and the same sampled poses |
| 8 | **Sample + review:** Alden Wayfarer walking about Hillside (idle, walk, run from his speed; root motion; a staff in his hand), a crowd benchmark, performance notes, the 8C review | The 8C gate |

## Highest risks (settle early)

1. ~~**Test characters we may publish.**~~ **Settled 2026-11-20:** CC0 only, no hand animation needed from Tom
   ("I'm not an animator ... we will work on what we can get"). In `tests/data/animation`: **RobotExpressive**
   (Quaternius, CC0: 14 clips, mostly rigid parts on bones, an armature scaled ×100, face morphs) and the glTF
   tutorial's **SimpleSkin** (CC0, exact skinning values). Smoothly skinned humanoids for goldens and retargeting
   (two proportions, T- and A-pose, Rigify-style and Mixamo-style names) are **generated by our own script**, as
   8B's test sprites were. The robot shows two needs the plan hadn't named: meshes riding on bones without a skin
   follow the pose too (part 1), and clips' morph-target channels are skipped with a warning (morphs are later).
2. **What Blender actually exports.** Armature scale (Mixamo's 0.01), non-uniform scale in bones, several skins in
   one file, joints outside the skin's root, Rigify's control bones if "Deform bones only" is off: part 1 meets
   real files early and warns in plain words rather than guessing.
3. **Skinning and batching.** ADR-032 batches identical meshes into one draw; skinned characters can't share a
   pose. Each instance needs its own palette offset; a crowd must still be a handful of draws, not hundreds.
4. **Retargeting quality.** Rest poses differ (T vs A, rolled bones); a naive rotation copy twists limbs. The
   transfer must work relative to each rig's rest pose, and the tests must measure bone directions, not eyeball.
5. **Determinism with blending.** Quaternion blending must be bit-stable (fixed order, no library slerp that an
   optimiser may vectorise differently), as 8B's own sine and cosine were.

## Part 1 result (2026-11-20)
Done. Skins from glTF (joints mapped to the scene's nodes, inverse bind matrices, up to 8 influences read and the
strongest 4 kept and renormalised, plain-words warnings), skinned meshes drawn by linear blend skinning in the vertex
shader (skin data and per-frame palettes read through buffer device addresses: no new pipelines), shadows included,
in games and the editor's viewport (posed picking and framing on the CPU). **Changed from ADR-036's proposal: bones
are entities**, as every glTF node already was when placing a model; a skinned mesh's entity lists its joints, so
posing those entities poses it, and rigid characters (the robot) work the same way. Proven: unit tests (SimpleSkin's
weights and inverse binds; its top joint bent 90 degrees moves vertices to the hand-worked places; the robot's skinned
hands follow their joints in its odd rest pose; Ward bound and posed, by size); golden `Skinning` (Ward in his bind
pose, Ward posed with a bent knee and elbow, the robot); `render.skinning_cost` (100 characters 0.2-0.4 ms of GPU,
gathering 1.2 ms of CPU); the editor smoke test (a posed Ward placed, 19 entities, drawn, with Vulkan validation on).
Test characters: CC0 RobotExpressive and SimpleSkin, our generated Ward (`make_test_characters.py`). Found on the way:
a palette offset counted in joints where rows were meant (the first character drew right by luck). Not done: the
gizmo doesn't bend a skinned mesh mid-drag; levels of detail don't switch skinned models; morph targets.

## Open questions

1. **Alden for the sample.** There's no rigged Alden and Tom doesn't animate by hand. Options for part 8: a blocky
   stand-in built by script from his concept sheet's description, animated by retargeting the CC0 robot's clips
   (Tom to approve the look), or the robot itself on Hillside with Alden waiting for a real model.
2. ~~**Which CC0 test characters?**~~ Settled (risk 1 above).
3. **FBX:** keep part 7 (research, then maybe an importer), or drop it and stay glTF-only for now?

## Not in 8C (planned later)
IK solvers, look-at and aim constraints, foot placement on uneven ground, ragdolls and blending into them (spec
18.2, with physics), morph targets and facial animation, the sequencer and cinematics (18.4), an animation studio
for posing and keying in the editor (ideas inbox: after 8C), animation compression, GPU sampling, dual-quaternion
skinning, network replication of animation state.
