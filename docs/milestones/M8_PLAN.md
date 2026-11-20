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
| **8C: Skeletal animation** | Skinned meshes and animations from glTF (any rig Blender exports), an animation player, blending, a simple state machine, root motion, an editor preview; **retargeting** through a humanoid bone map with presets for **Rigify** (deform bones), **Mixamo**, the **Unreal mannequin** and Blender's metarig; Alden Wayfarer as the test character | **Done** ([review](M8C_REVIEW.md)) |
| **8D: Water** | Water bodies (lakes, a sea plane), reflections, shorelines that read the terrain, and **underwater** environments (light absorbed with depth, fog, a surface seen from below), built on 8A's environments | **Next** ([parts below](#8d-water)) |
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
| 2 ✅ | **Clips and the player:** animations from glTF (translation, rotation, scale; step, linear and cubic-spline), the deterministic sampler, the `animator` component (clip, loop, speed, time offset), scripts (play, stop, speed, events, end) | Sampled poses equal glTF's own keys and interpolation (unit); a golden at fixed times; a built game samples identically; Lua and C# play a clip and hear its end |
| 3 ✅ | **Blending and the controller:** cross-fades, 1D and 2D blend spaces, layers with bone masks, additive layers, the `.nooranim` controller (parameters, states, transitions with conditions) | Blend maths (quaternion sign, normalisation, weights summing to 1) in unit tests; transition timing exact to the frame; an idle-walk-run blend golden sequence |
| 4 ✅ | **Root motion and characters:** root motion into the character controller, sockets (props on bones), bone transforms for scripts; Create ▸ Player gets an animated character (spec 18.5) | A walk with root motion covers its clip's distance (within a centimetre) and stops at a wall; a sword in a hand follows it in a golden; the same in a built game |
| 5 ✅ | **Retargeting:** the humanoid bone map, the four presets and auto-detection, manual mapping, rest-pose-relative transfer, hips scaling | One clip on rigs of different proportions and rest poses (T and A): bone directions within a few degrees at key frames (unit); a side-by-side golden |
| 6 ✅ | **The Animation panel:** clips with a scrubbable timeline, playback speed and looping, the skeleton shown in the viewport, the controller's states and transitions edited, the bone map with its detected preset and warnings | An editor smoke test scrubs a clip, edits a transition and sees the preview follow, and maps a rig |
| 7 ✅ | **FBX, research first:** ufbx (MIT) checked against Blender, Mixamo and Unreal FBX files; if it holds up, an importer into the same skeletons and clips as glTF; if not, written down why | A go/no-go note with the files tried; with a go, an FBX and its glTF export import to the same skeleton and the same sampled poses |
| 8 ✅ | **Sample + review:** Alden Wayfarer walking about Hillside (idle, walk, run from his speed; root motion; a staff in his hand), a crowd benchmark, performance notes, the 8C review | The 8C gate |

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

## Part 2 result (2026-11-20)
Done. Clips from glTF (translation, rotation and scale; step, linear with shortest-path slerp, cubic spline; morph
channels skipped with a warning), `asset::sample` (a pure function of clip and time), the `animator` component on a
model's root (animation, loop, speed, time offset; started and paused by scripts) and `game::AnimatorSystem` (poses the
bones at the frame's end time, posts the end of a script-played one-shot). Placing an animated model lists its node
entities on the root; older scenes fall back to names. Scripts: sprites' play, stop, speed and `on_animation_end`
now drive animators too (one API, Lua and C#). Proven: unit tests against hand-worked values (Ward's generated clips:
Wave 45 degrees at 0.5 s by slerp, Nod's step at 0.5 s, Glide's Hermite 0.4375 m at 0.25 s and 0.75 m at 0.5 s; the
robot's 14 clips sound at every time) and through a world (bones by node list and by name; loop, speed and offset;
equal fingerprints; a Lua script playing, stopping and hearing one end); golden `Animation` at 0.75 s (Ward walking,
waving, nodding; the robot walking); `cli.animation_scripts_build_identically` (Lua in the project and the built game,
C# in the built game: both end events at the same frames, bone fingerprints equal); `render.animation_cost` (100
walking characters: +0.43 ms game update, +0.2 ms GPU, gathering 1.24 ms). Found on the way: bones were posed at the
frame's start time while sprites showed its end (a frame behind; a loose test tolerance had hidden it); the test
scripts summed `dt` in doubles (Lua) and floats (C#) and acted a frame apart (counted in frames now, and documented).
Not done: no edit-mode preview (part 6); changing clip snaps (part 3).

## Part 3 result (2026-11-20)
Done. `asset/pose_blend.h` (nlerp, weighted mixes, additive deltas, 1D and gradient-band 2D blend-space weights), the
`.nooranim` controller (`asset/anim_controller.h`: parameters, layers with masks and override or additive modes, states
with a clip, a blend space or nothing, transitions with conditions, exit times and fades) and its player, run by the
`AnimatorSystem` (stepped in `update`, posed in `late_update`); cross-fades for plain clips; scripts set parameters and
triggers and cross-fade (Lua, and C# through the script ABI's v5). Ward got Idle, Run, a wave hello (Raise) and Lean,
and `Ward.nooranim`. Proven: unit tests (blend maths by hand: nlerp's midpoint, cancelling rotations, weighted
translations, additive deltas adding back exactly and by half, blend-space weights exact at samples and summing to 1;
parse errors in plain words; a 1D blend equal to its two clips mixed at the shared phase; a trigger's transition on its
frame, a 0.1 s fade in exactly 6 frames, Nod ending on frame 60 and returning on 61; a masked layer moving only the
arm; an additive lean of 20 degrees, and 10 at half weight; a 2D space exact at a sample; two players bit-identical
after 200 frames; a Lua script setting a trigger and a bool and hearing the state end; a script cross-fade at 40 %
equal to the two clips blended); goldens `Blending_20/50/80`; `cli.animation_controller_build_identically` (a script
driving the controller: Lua project, Lua and C# built game, equal bone fingerprints); `render.animation_cost` with
100 characters on the controller (+1.0 ms of game update, budget 3). Found on the way: one-shots' ends were noticed a
frame after their last pose (now the same frame); in a test, scripts added after the animator ran a frame late, the
opposite of `make_world`'s order; part 2 hadn't updated the Lua and C# API pages (done now). Not done: animation events
(part 4); no editor for controllers yet (part 6: text until then); transitions can't interrupt a fade in progress.

## Part 4 result (2026-11-20)
Done. Root motion (`asset::root_motion_node`, `root_delta`: whole cycles across a loop, clamped one-shots; the
animator's `"root_motion"` for clips, cross-fades and controllers), given to the nearest character controller as
velocity or to the model; animation events (`asset::crossings`; on the animator per clip and on controller states;
`on_animation_event` / `OnAnimationEvent`); `bone(name)` for scripts (props on bones were already free: bones are
entities); Create ▸ Player (third person) (Ward and his controller embedded in the editor, an orbiting camera rig,
`Scripts/ThirdPersonPlayer.luau`); the player report gives each animated character's position. Proven: unit tests
(root movement by hand: 1.4 m a cycle across the wrap, 3.15 m over two and a quarter, a one-shot's 1 m clamped;
event crossings; a walk covering 3.5 m within a centimetre facing +Z and turned to +X, the hips held over the
origin; through a character controller 4.2 m within a centimetre, and a wall stopping it at its face; footstep events
counted by a script; a controller's root motion and events at double speed; a sword on the hand bone exactly where
the hand puts it); golden `Root_Motion` (the sword in his hand mid-walk; Ward against a wall);
`cli.root_motion_build_identically` (a walk on a character controller, sped up by its fourth footstep, stopped by a
wall: Lua project, Lua and C# built game: the same pose and the same place to the millimetre); the editor smoke test
creates the third-person player, walks him with W (1.55 m in about a second), sees him turn and his walk play.
Found on the way: the smoke test's picture first looked through the first-person player's camera (Play takes the
first camera); the check now takes that player out for the moment. Not done: root motion turning (yaw); jumping
and falling animation states are the game's to wire; the template's movement is script-driven by default.

## Part 5 result (2026-11-20)
Done. `asset/retarget.h`: the humanoid bone map (22 bones, 15 required), detected by the Rigify, Mixamo, Unreal
mannequin and Blender metarig presets or by common names, corrected in a model's `.noormeta`
(`settings.bone_map`), with warnings in plain words; the Retargeter (per-bone rest corrections by direction, the
source's turn from rest in model space, hips moved by leg length) and `bake` (an ordinary clip of the target). The
animator's `"retarget_from"`; controllers' masks may name humanoid bones or, retargeted, the clips' model's bones.
Two new generated bodies: Mast (2.0 m, Mixamo, T-pose, a 0.01 armature) and Bollard (1.45 m, Rigify with twist
bones, A-pose, rolled Blender bone frames). Proven: unit tests (each preset and common names detected, corrections
and warnings; both bodies skin to themselves at rest; Walk, Run and Raise on both: limb directions within two degrees
of Ward's at keys and between them; an A-pose arm lifted only where the clip moves it; the hips moved by leg length;
baking bit-identical; Ward's walk on Mast through the animator covering 2 x 1.4 m x his leg ratio in metres; Ward's
controller on Mast, its mask translated); golden `Retargeting` (Ward, Mast and Bollard walking and waving on one
controller, a Bollard on root motion); `cli.retargeting_build_identically` (the controller script on Bollard: Lua
project, Lua and C# built game, the same pose). Found on the way: root motion ignored an armature's scale (fixed);
Ward.nooranim's mask named Ward's bone, which other bodies don't have (masks now translate). Not done: fingers;
foot IK; the editor's view of the map (part 6).

## Part 6 result (2026-11-20)
Done. The editor's **Animation panel** (a tab beside the Content Browser): the scene's characters (following the
selection); clips played and scrubbed on a timeline with speed, looping and *in place*; the skeleton drawn over the
model in the viewport; the controller's layers as a state graph, a state or a transition previewed (the fade shaded on
the timeline) or the whole controller run with preview parameters; states and transitions edited (name, clip, loop,
speed, entry; from, to, fade, exit time, conditions; added and deleted) and saved as you go; the bone map with its
preset, warnings and 22 bones, corrected into the `.noormeta`, the picked bone lit up in the viewport. The engine side
is `noor_doc_animation` (API v27); the preview poses the render snapshot only, through `game::AnimationAssets` (split
out of `AnimatorSystem`, so the preview and games share the loading). Proven: API tests (clips, skeleton and map read;
a correction written to the `.noormeta`, warned about and taken back; refusals; scrubbing: the same moment the same
pose and not one scene transform or revision changed; a controller run, a state and a transition previewed, the fade
weight exact to the step; a saved slower fade followed by the preview; a controller that doesn't parse not written);
the editor smoke test (Ward picked by selecting him, Walk plays and is scrubbed, the scene untouched; Arm's Off → Wave
at half its fade, the fade edited to 0.6 s and the preview at a quarter; his bone map shown, LeftHand unmapped (saved,
warned), mapped elsewhere (lit up), put back; hiding the panel ends the preview). Not done: blend-space samples, layers
and parameters edited in the file; controller edits aren't on the undo stack; graph positions aren't saved.

## Part 7 result (2026-11-20)
Done: **go** ([FBX_RESEARCH.md](../animation/FBX_RESEARCH.md)). ufbx 0.23.1 (MIT or public domain), pinned by hash.
`asset/fbx_import.h` reads FBX into the same `ImportedModel` as glTF (skeletons, skins, clips, meshes, materials and
embedded or neighbouring textures), and `asset/model_import.h` makes every model load take either; the registry calls
an FBX a model (importer "fbx"). Clip files of their own play on a character with the same skeleton, any rig
(`"retarget_from"` copies by bone name; humanoids with other names are retargeted as before). Tried, each file read by
NoorEngine and by Blender and compared: our characters through Blender 5.2 and 3.3 (exact at rest; clips within
0.001 mm with Blender's curve simplification off, within 1.6 mm with it on), 97 files of FBX SDK 2020 store-bought
creatures (joints within 0.14 mm, shapes within 0.002 mm, all 79 clips), 18 Synty files from Maya (within 0.014 mm),
12 Blender and Meshy statics (up to 1.2 million triangles). Every file imported. Proven: unit tests "fbx" (Ward, Mast,
Bollard and Blender 3.3's Ward against Blender's glTF of the same scene; Ward's clips from a clip-only file copied
exactly; other skeletons refused; broken, empty and oversized files refused in words; the registry); `noor_cli
compare-models`. Found on the way: Blender's FBX exporter keys every bone with a leftover pose and simplifies curves;
its 3.3 glTF exporter and importer get Ward wrong in places; a take's span vs Blender's absolute times. Not tried:
Mixamo's and Unreal's own FBX files (FBX SDK too, but untested), ASCII and pre-7 FBX. Not imported: blend shapes,
cameras, lights.

## Part 8 result (2026-11-20)
Done: the 8C gate passes ([review](M8C_REVIEW.md)). Alden Wayfarer, made by `tools/samples/make_alden.py` from the
concept sheet's description (approved by Tom), on Ward's skeleton, with his own controller (Idle, WalkForward and
RunForward by speed) and his staff as a separate model; `Hillside_Alden.noorscene` (written through the editor API by
`add_hillside_alden.py`): he wanders a loop by the house on root motion, his staff on his hand's bone. Proven: golden
`Hillside_Alden`; `player.hillside_alden` (the built Hillside game: every bone and his place to the millimetre, fifteen
seconds in). The crowd benchmark (`measure_hillside_alden.py`): 100 Aldens, each with a character controller, script
and controller, cost no GPU worth measuring and 1.9 ms of game update plus 2.0 ms of gathering; 250, 4.6 and 4.4 ms.
Noor painted all ten "Alden discovers" pictures, M1 to 8C, the same day ([ALDEN_DISCOVERIES.md](ALDEN_DISCOVERIES.md)).

## Open questions

1. ~~**Alden for the sample.**~~ **Settled in part 8:** a blocky Alden built by script from his concept sheet's
   description, on Ward's skeleton with Ward's motions (Tom approved the look).
2. ~~**Which CC0 test characters?**~~ Settled (risk 1 above).
3. ~~**FBX:**~~ **Settled in part 7:** kept, researched, and a go (FBX_RESEARCH.md).

## Not in 8C (planned later)
IK solvers, look-at and aim constraints, foot placement on uneven ground, ragdolls and blending into them (spec
18.2, with physics), morph targets and facial animation, the sequencer and cinematics (18.4), an animation studio
for posing and keying in the editor (ideas inbox: after 8C), animation compression, GPU sampling, dual-quaternion
skinning, network replication of animation state.

---

# 8D: water

## Where 8D starts

- **There's no water.** Hillside has a valley and no lake; the open question from 8A ("a deep-sea environment
  before 8D?") is still open; Thalassa (Tom's deep-sea game) and Thornwake's rivers both want it.
- **What the renderer has:** a see-through pass after the opaque scene, sorted back to front (glass, particles);
  the sky, sun and environment lighting rebuilt on the GPU (8A: reflections of the sky are already there to sample);
  height fog and aerial perspective; MSAA (4x by default); cascaded sun shadows.
- **What it hasn't:** nothing reads the scene behind a see-through surface. There's no copy of the opaque scene's
  colour or depth, so no refraction, no "how deep is the water here", no soft edges where water meets the shore; no
  screen-space reflections; no planar reflections.
- **What else helps:** terrains with height fields the CPU can read (sculpted ones too), Jolt physics with a
  character controller, deterministic fixed steps and built-game checks, environment assets with presets and
  weather zones (8A), particles that collide with planes and spawn sub-emitters (8B), and the editor's gallery
  pattern (Environments, Effects).
- **The spec (17.1, NoorWater)** asks for distinct bodies (ocean, lake, river, pool, underwater volumes), shorelines
  and lake levels, rivers along splines or flow networks, waves for open water, shallows and rivers, PBR reflection,
  refraction and transparency, underwater fog and light falloff, foam, wetness, ripples, wakes, wind and weather,
  a query API for boats, swimmers, physics and gameplay, and debug views (normals, flow, displacement, depth,
  cost). It says plainly that an art-directed approximation is fine if it's consistent and disclosed: 8D is not a
  fluid simulation.

## Decisions (ADR-037, accepted 2026-11-20)

- **A `water` component on an entity**, with a shape: a **box** (lakes, ponds, pools: a level surface inside a
  footprint, and a volume under it), an **endless sea** (a surface to the horizon, following the camera), and later
  a **river** (a spline). Its look in a **`.noorwater` asset** with built-in presets and overrides on the component,
  like environments and effects.
- **The scene behind the water is read once a frame**: when water is in view, the opaque scene's colour and depth
  are resolved and copied after the opaque pass. The water then knows how thick it is at every pixel: its colour is
  light absorbed by that thickness (red first, then green: Beer-Lambert per channel), the shore fades in softly,
  foam gathers where it's shallow, and what's under it is bent a little (refraction by offsetting the lookup).
- **Reflections: the sky and sun always, nearby things by screen-space reflection** where the screen has them,
  falling back to the sky where it hasn't. Planar reflections (a mirror-perfect lake, at the cost of drawing the scene
  a second time) are not in 8D (Tom, 2026-11-20).
- **Waves are a formula, not a simulation**: a handful of Gerstner waves set by the wind and the preset, plus
  scrolling detail normals. One formula, evaluated in the vertex shader for drawing and on the CPU for gameplay
  (`water_height(x, z)` for scripts, buoyancy, swimming), a pure function of position and game time, so built games
  agree. Gameplay reads only the CPU's answer. An FFT ocean only if Gerstner waves can't look right.
- **Underwater belongs to the environment**: an `underwater` block (absorption, scattering colour, visibility,
  caustics) used whenever the camera is inside a water volume, so a weather zone can change it like anything else.
  A **deep sea without a surface** is simply a water volume whose surface is far above (8A's open question: yes).
- **Floating is NoorEngine's, pushing is Jolt's**: buoyancy is worked out from sample points on each body against
  the water's height (submerged fraction, drag), applied as forces at the fixed step. Characters wade (slowed) and,
  in deep water, swim (a state scripts and the animator can read).

## Parts (each ends tested, documented and pushed)

| Part | What | Proves |
|---|---|---|
| 1 ✅ | **The water surface:** the `water` component (box, endless sea), the scene's colour and depth copied once a frame, absorption by thickness, refraction, a soft shoreline, sky and sun reflections with Fresnel, fog and aerial perspective over it; `.noorwater` with a first preset | A lake on Hillside as a golden; thickness and absorption against hand-worked values (unit); the copy's cost measured |
| 2 ✅ | **Waves:** Gerstner waves from the wind and preset, detail normals, the endless sea's grid following the camera to the horizon; `water_height(x, z)` for Lua and C# | The CPU's height equals what's drawn (a marker floated at the CPU's height sits on the rendered surface, read back); identical in a built game; a sea golden at two moments |
| 3 ✅ | **Shorelines and reflections:** each body's depth map from the terrain under it, waves calming in the shallows, foam and wet sand at the waterline; screen-space reflections with the sky behind them | Waves damped by depth (unit); a shoreline golden; reflections of the house in the lake that fade to sky at the screen's edge |
| 4 ✅ | **Underwater:** absorption and fog with depth, the surface seen from below (the bright window overhead, total internal reflection beyond it), caustics on what's below, the waterline across the screen when the camera is half in; underwater in `.noorenv`, a deep-sea preset | Goldens under the lake, in the deep sea, and at the waterline; walking a camera down through the surface without a pop |
| 5 ✅ | **Water in the game:** buoyancy for physics bodies, wading and swimming for characters (`on_water` events, the depth at the feet and head), particles splashing on the surface (8B's planes), rain rippling it, wind roughening it | A crate floats at the depth its density says, within a centimetre, and bobs identically in a built game; a character slows in water and swims in deep water; splash counts exact |
| 6 ✅ | **Rivers:** a spline water body following the ground, flowing (flow-scrolled normals and foam, speed along the spline), joining a lake; the flow speed for scripts and buoyancy | A river golden; a floating crate carried downstream at the flow's speed |
| 7 | **The Water panel and debug views:** a gallery of presets (Lake, Pond, Sea, Tropical, Swamp, River, Deep sea) like Environments, live editing, debug views (depth, flow, wave displacement, normals, cost) | API tests (presets, apply in one undo step, edits reach the game); the editor smoke test edits a lake and sees it change |
| 8 | **Sample + review:** Hillside gets its lake (Alden at the shore, crates bobbing, rain on the water), a seaside or deep-sea scene, a cost table, the 8D review | The 8D gate |

## Highest risks (settle early)

1. **Reading the scene behind the water.** Copying colour and depth after the opaque pass means resolving MSAA (or
   reading it multisampled) and restructuring the frame's passes; it has to cost little when water is in view and
   nothing when it isn't. Part 1 measures it before anything is built on it.
2. **Reflections people expect.** A calm lake that reflects only the sky looks wrong; screen-space reflections miss
   whatever is off screen (the far shore at the top of the picture, the underside of a bridge). Settled in part 3
   with before/after pictures; planar reflections stay possible later for mirror lakes.
3. **One wave formula on two processors.** The shader's floats and the CPU's must agree closely enough that a
   floating crate sits on the drawn surface; gameplay uses only the CPU's answer, drawing only the GPU's, and a test
   measures the gap.
4. **The endless sea.** A surface to the horizon needs a grid that follows the camera without swimming or shimmering,
   and precision far from the origin (Baltic Drive's moving origin is the reference if it comes to that).
5. **The waterline.** A camera half in the water sees two worlds split by a wobbling line; the classic place for
   pops and seams. Part 4 tests a camera walked down through the surface.

## Part 1 result (2026-11-20)
Done. The `water` component (a box: a level rectangle, sized by `size` and the entity's scale, turned with it; a sea:
level at the entity's height, under the camera out to 20 km) with its look from a built-in preset or a `.noorwater`
asset and its own fields over them (`asset/water_asset.h`: absorption per metre or a tint and clarity, scatter,
refraction, shore fade, roughness, ripples; one preset so far, Lake). When water is in view the scene pass is split:
opaque and sky, then the colour (resolved) and depth (with MSAA, sample 0 resolved by the pass; without, copied) go to
two images in set 0 (bindings 7 and 8), then the see-through pass draws the water first (writing depth), then glass and
particles. Each water pixel reconstructs what's under it: Beer–Lambert absorption over the vertical depth plus the
view path, refraction by the ripples' slope (not onto things in front of the water), light scattered inside lit by the
sun (shadowed) and sky, the sky reflected by Schlick Fresnel from the environment cube, a GGX sun glint, aerial
perspective and fog over the water's own light, a soft shore over `shore_fade` metres of depth. Ripples are six sine
waves on the game's clock, faded out when smaller than a few pixels (they aliased into a grid) and turned into
roughness instead. The editor: Create ▸ Water, the preset chosen from the engine's list (`noor_water_presets`, editor
API v28), the Render panel counts water draws; the GPU timings gain **Scene copy** and **See-through** passes (eight
in all). Proven: unit tests "water" (6: transmittance and tint-to-absorption against hand-worked values, the Lake preset
worked through from its sRGB colours, bad values, files, components to draws including a sea); goldens `Water_Pool`,
`Water_Sea` and `Hillside_Lake` (`tools/samples/add_hillside_lake.py`: a hollow sculpted through the editor API in the
scene's own sculpt layer, the water's edge checked to be on higher ground); the editor smoke test (Create ▸ Water drawn,
undone). **Measured** (`tools/samples/measure_hillside_lake.py`, RTX 3080, 1280×720, 4× MSAA): the copy 0.02 ms, the
lake 0.035 ms, the split pass about 0.03 ms more: about 0.1 ms with water, nothing without. Highest risk 1 settled.
Not done: the absorption counts the surface-to-camera stretch as air (an approximation, documented); no planar or
screen-space reflections yet (part 3); the sea is flat (part 2).

## Part 2 result (2026-11-20)
Done. Four Gerstner waves (the main one from the look's `wave_height`, `wave_length`, `wave_direction` and `choppiness`,
three smaller ones at fixed proportions), each at deep water's pace with its phase in double precision; the wind (the
particles' ground wind, `wind` x 0.21 U^2/g) raises them without changing their lengths or directions. One formula
(`game/water.h`) evaluated on the CPU for gameplay (the height found by stepping back from the sideways motion; the
exact normal from the surface's tangents: GPU Gems' shorter formula was 2 cm/m off on choppy crests, caught by the unit
test) and in water.hlsl's vertex shader to draw. The sea's grid: rings round the camera out to 20 km (256 round, about
440 out); a box with waves gets cells of a sixth of its shortest wave; waves too short for the grid fade out where drawn
(gameplay keeps them). `noor.water_height` / `noor.water_normal` (Lua) and `Water.Height` / `Water.Normal` (C#, rt API
v7) through a World water query that `game::WaterSystem` answers. The "Sea" preset. Proven: unit tests (deep water's
pace, phases an hour in, the wind's share, heights where the waves moved the surface, the normal against the measured
slope, scripts reading the highest body); `render.water_readback` (the depth read back without MSAA at 24 x 24 pixels,
each point against `water_height`: 416 pixels within 20 m, at most 6.2 mm apart, 2 mm within 10 m; the player's
`--water-probes`); `player.water_waves` (four Lua-floated buoys in the same place to the bit in the built game); goldens
`Water_Waves_1s` and `_4s`; the part 1 goldens unchanged. **Measured:** the wavy sea 0.1 ms (670,000 vertices). Not done:
far out the drawn waves are calmer than gameplay's; waves ignore depth until part 3. Tom asked for crisp, stylised
waves later (IDEAS inbox: crest glow, foam on steep crests, more waves).

## Part 3 result (2026-11-20)
Done. Each water body's depth map from the terrains under it (`game::water_depth_map`: a box's footprint, a sea's
terrains; up to 256 samples a side; built identically by gameplay and drawing, kept while unchanged; uploaded as an R32
float texture in the texture table). Each wave keeps `tanh(k depth)` of itself (a Padé fit, `shoal()`), read at the
point at rest with the same bilinear arithmetic on the CPU and in the vertex shader (exact texel loads), the normal
including the shelving term. Foam at the shore and round things in the water (shallower than `foam_depth`, from the
copied depth), on folding crests (the Jacobian of the waves' sideways motion, `crest_foam`), laced with drifting noise.
Wet ground: a band drawn before the surface multiplies what's under it, and the water darkens what's seen through it.
Screen-space reflections: 20 growing steps through the copied depth, narrowed down by 5 halvings, fading to the sky at
the screen's edges, the march's end and on rough water, skipped where Fresnel makes them invisible. Per-draw settings in
a storage buffer (set 0, binding 9). New look fields `foam`, `foam_depth`, `crest_foam`, `wet`, `wet_height`,
`reflections`; the Lake and Sea presets use them. Proven: unit tests (3 new, 12 in all: the fit, dry ground, the normal
over a shelf, depth maps from a flat terrain); `render.water_readback_shore` (over a beach: 322 pixels within 20 m, 309
over shallow water, at most 7.4 mm from `water_height`); goldens `Water_Shore` and `Water_Reflection` (the house in the
pond, its roof off the top of the screen fading to sky); the part 1 and 2 goldens redrawn. **Measured:** the wavy sea's
water 0.32 ms (was 0.11: reflections 0.14, foam 0.03). **Changed from the plan:** "the house in the lake": the
Hillside house is 80 m from the lake behind a rise, so the far shore hides its reflection from every camera that sees
the lake; the proof is a test pond with a house on its bank, and the Hillside lake reflects its trees. Not done: only
terrains count as ground; waves don't slow, turn or break in the shallows; the wet band is fixed.

## Part 4 result (2026-11-20)
Done. Light under a body's level (inside its footprint, the first eight bodies) comes through the water in every lit
mesh (`water_light.hlsli`): the sky's `exp(-absorption depth)`, the sun's over its refracted slant, with caustics (two
layers of drifting cells bent by a swirl, fading in over 25 cm and out with depth); local lights undimmed. From inside
(the camera in a body), two full-screen passes after the surfaces: what gets through multiplied, the scattered light
added (16 jittered steps: sunlit with forward scattering and one shadow tap, skylit, dimmer with depth, broken into
shafts), each pixel deciding by its near-plane point whether it's under the surface (the waterline across the screen,
a dark line along it). The surface from below: Snell's window (the sky, what's above traced through the copy, the
sun's glint), total internal reflection beyond it showing the water's glow. The exposure opens by half the stops the
water has taken at the camera's depth. The environment's `underwater_*` settings (blended by weather zones) replace a
body's own while the camera is in it. Look fields `caustics`, `shafts`; the "Deep sea" preset. Proven: unit tests (2
new, 14 in all); `render.water_descent` (a camera walked down through the pond's surface: the biggest step 16% of
the whole change); goldens `Water_Underwater`, `Water_DeepSea`, `Water_Waterline`; the other water goldens redrawn with
caustics. **Measured:** from inside, about 0.3 ms. Not done: see-through things and particles aren't fogged; one body from
inside; depth from the level, not the waves; caustics and shafts are patterns, not traced light. Thalassa (Tom's
deep-sea game) is the mood this was tuned for (Tom, 2026-11-20).

## Part 5 result (2026-11-20)
Done. Dynamic bodies float (`physics.h`): cut into cells (a box 3 x 3 x 3, other shapes the inside cells of 5 x 5 x 5),
each wet cell lifted by the water it displaces through the World's water query and slowed in proportion; a body floating
at the surface may not sleep (Jolt froze one 8.5 mm off; woken, it looped for ever). `"buoyancy"`, `"water_drag"` on the
rigidbody. Characters wade (slower with depth) and swim past `swim_depth` (chest at the surface, `swim_speed`), with a
state and depths for scripts (`e:character_water()`, C# `WaterState`/`FeetDepth`/`HeadDepth`, rt API v8) and a Water
event (`on_water`, `OnWater`). Particles collide with the water (`"collide": {"water": true}`) and fire `"on": "water"`
sub-emitters; the player counts splashes. Rain rings and wind roughens the drawn surface. Proven: unit tests (planks of
three densities within a micrometre of the textbook depth, sinking, riding waves identically, a character's wading speed
and swimming height and events, exact splash counts); `player.water_float` (the rainy sea: floating bodies identical in
the built game, 12 splashes each); golden `Water_Float`. Not done: currents, diving, a swim clip (Alden's is part 8),
bodies splashing by themselves, floating's CPU cost (part 8). Changed from the plan: "wind roughening it" is the drawn
ripples only (gameplay's waves already grow with the wind since part 2).

## Part 6 result (2026-11-20)
Done. `"shape": "river"`: points (upstream first, each at the surface's height, optional width), a Catmull-Rom curve
sampled about every metre (`game::river_path`, at most 512 samples), level across, one `speed`. Gameplay: `river_at`
(the nearest stretch within the width: height, direction, slope), the World's water query gains `flow`; floating bodies
are dragged toward the flow (carried), swimmers drift, waders are pushed by part of it; `noor.water_flow` / `Water.Flow`
(rt API v9). Drawn as a ribbon from the course in an R32 float texture (a column per sample), ripples and foam in the
river's own coordinates sliding downstream, white-water streaks from 8 cm a metre. Proven: unit tests (the course and
points on it by hand; a crate carried at 1.5 m/s within 2%, a swimmer drifting); goldens `Water_River` (a generated
valley with a meander, a steep stretch, a crate and a barrel riding it) and `Water_River_Mouth` (into the lake). Not
done: one speed across, no view from underneath and no light dimmed under rivers, no waterfalls, no wet bank, no spline
tool (part 7's panel). "A river following the ground": the points carry the heights (a generator can take them from the
ground; Thornwake's hydrology does exactly that, in the ideas inbox).

## Open questions

All four settled by Tom on 2026-11-20, each as recommended:
1. ~~**Reflections**~~ **the sky plus screen-space reflections**; planar reflections stay possible later.
2. ~~**Which water first**~~ **the calm Hillside lake** gets the first tuning (part 1); the sea and the deep sea follow.
3. ~~**Rivers**~~ **a spline surface over the existing ground in 8D** (part 6); carving river beds comes later, with
   NoorRoads.
4. ~~**Swimming**~~ **a generated swim clip for Alden** (made by `make_alden.py`, like his walk), for part 5 and the
   sample.

## Not in 8D (planned later)
Fluid simulation (Thornwake's local water simulation, D22, is the reference when it comes), terrain carving by rivers
and roads (with NoorRoads), planar reflections, wakes behind moving boats, interactive ripples from
every footstep, waterfalls as their own effect, ice and frozen water, volumetric light shafts underwater (with NoorSky
phase C's volumetrics), boats and vehicles as gameplay templates.
