# Changelog

Technical, honest, newest first. "Works" means tested; anything else says what it is.

## [Unreleased] — Milestone 8 in progress (8A: sky, atmosphere and environments)

### 2026-11-20 — Milestone 8C, part 7: FBX
**Added**
- **FBX import** through ufbx 0.23.1 (MIT): skeletons, skins, clips (each take, baked at 60 a second), meshes,
  materials and textures, into the same models as glTF (`asset/fbx_import.h`); `asset/model_import.h` picks the
  importer by extension and every model load uses it; `.fbx` files are assets (importer "fbx").
- `"retarget_from"` copies clips from a file with the same skeleton (any rig: FBX clip files beside their character).
- `asset/model_compare.h` and `noor_cli compare-models`: two imports of one model compared (skeleton, joints through
  every clip, shapes). `tools/samples/make_fbx_test_files.py` (Blender: FBX and its glTF from one scene, or Blender's
  reading of any FBX) and `tools/samples/fbx_research.py`.
- The research note, `docs/animation/FBX_RESEARCH.md`: 133 files tried, a go.

**Tests:** unit tests "fbx" (4); test files in `tests/data/animation/FBX` (Blender 5.2 and 3.3 exports of our characters).

### 2026-11-20 — Milestone 8C, part 6: the Animation panel
**Added**
- The editor's **Animation panel**: a scene character's clips played and scrubbed on a timeline (speed, loop, in
  place), the skeleton drawn in the viewport; its controller as a state graph, states and transitions previewed and
  edited (saved as you go), the whole controller run with preview parameters; its humanoid bone map with preset and
  warnings, corrected into the model's `.noormeta`, the picked bone lit up.
- Editor API v27: `noor_doc_animation` (`model`, `set_bone`, `controller`, `save_controller`, `characters`,
  `preview`); the viewport draws the previewed skeleton.
- `asset::AnimControllerPlayer::set_automatic` and `state_length` (for previewing one state or transition).
- Content Browser: Edit on a `.nooranim` opens it in the Animation panel.

**Changed:** `game::AnimationAssets` (models with retargeting, controllers) split out of `AnimatorSystem`, shared by
games and the editor's preview.

**Tests:** API tests "animation api" (3); the editor smoke test's Animation panel checks (15).

### 2026-11-20 — Milestone 8C, part 5: retargeting
**Added**
- **Retargeting** (`asset/retarget.h`): humanoid bone maps detected from names (Rigify, Mixamo, Unreal mannequin,
  Blender metarig, common names) and corrected in a model's `.noormeta` (`settings.bone_map`); the `Retargeter`
  (rest-pose-relative, by bone direction; hips scaled by leg length) and `bake`.
- The animator's `"retarget_from"`: another model's clips, retargeted, as this model's own.
- Controllers' masks may name humanoid bones, or (retargeted) the clips' model's bones.
- `asset::posed_world`; generated test bodies Mast (Mixamo, T-pose, 0.01 armature) and Bollard (Rigify, A-pose).

**Changed:** skinned models placed in a scene list their nodes (as animated ones did), so they can play retargeted
clips.

**Fixed:** root motion now accounts for transforms between the root bone and the model (an armature's 0.01 scale).

**Tests:** unit tests "retargeting" (9); golden `Retargeting`; `cli.retargeting_build_identically`.

### 2026-11-20 — Milestone 8C, part 4: root motion and characters
**Added**
- **Root motion:** `asset::root_motion_node`, `asset::root_delta`; the animator's `"root_motion"` (clips, cross-fades,
  controllers' base layer) moves the nearest character controller (as velocity: walls stop it) or the model.
- **Animation events:** `asset::crossings`; `"events"` on the animator (per clip) and on controller states;
  `on_animation_event(name)` (Lua), `OnAnimationEvent` (C#); `runtime::Event::Kind::AnimationEvent`.
- `bone(name)` for scripts (Lua `e:bone`, C# `Entity.Bone`, `runtime::api::bone`).
- **Create ▸ Player (third person):** Ward on his controller as the player's model, an orbiting camera rig,
  `Scripts/ThirdPersonPlayer.luau`; Ward's model and controller embedded in the editor.
- Ward's WalkForward clip (1.4 m a cycle); the player report's animators carry their position.

**Changed:** the script ABI is v6 (`bone`; the `ANIMATION_EVENT` event kind).

**Tests:** unit tests "root motion" (3) and "animation events" and the controller/bone case; golden `Root_Motion`;
`cli.root_motion_build_identically`; the editor smoke test's third-person player.

### 2026-11-20 — Milestone 8C, part 3: blending and the controller
**Added**
- `asset/pose_blend.h`: `nlerp`, `blend` (whole or per bone), `mix` (weighted), `additive_delta`/`apply_additive`,
  `blend_weights_1d` and `blend_weights_2d` (gradient band).
- **Animation controllers** (`.nooranim`, importer `anim_controller`; `asset/anim_controller.h`): parameters (float,
  bool, trigger), layers (masks; override or additive; weights from parameters), states (a clip, a 1D or 2D blend
  space played in step, or empty), transitions (conditions, exit time, fade); `AnimControllerPlayer`.
- The `animator` component runs one (`"controller"`, `"parameters"`, `"speed"`; `"state"` written back); plain clips
  cross-fade (`"from"`, `"fade"`). The player's report shows a controller's state.
- Scripts: `cross_fade`, `set_animation_parameter`, `set_animation_trigger` (Lua); `CrossFade`,
  `SetAnimationParameter`, `SetAnimationTrigger` (C#).
- Ward's Idle, Run, Raise and Lean clips; `Ward.nooranim`; Blending scenes (`add_animation_scene.py part3`).

**Changed:** the script ABI is v5 (`animation_parameter`; sprite op `CROSS_FADE`). A one-shot's end is reported the
frame its last pose is shown (was a frame later). The Lua and C# API pages now cover animators.

**Tests:** unit tests "blend maths" (3), "controller files", "controller" (5), "animator with/without a controller";
goldens `Blending_20`, `Blending_50`, `Blending_80`; `cli.animation_controller_build_identically`;
`render.animation_cost` measures controllers too.

### 2026-11-20 — Milestone 8C, part 2: clips and the player
**Added**
- **Clips from glTF** (`asset::ImportedAnimation`, `AnimationChannel`): translation, rotation and scale; step, linear
  and cubic-spline; morph-target channels skipped with a warning.
- `asset/animation.h`: `sample` (deterministic: a pure function of clip and time), `rest_pose`, `slerp`,
  `find_animation`.
- **The `animator` component** (animation, loop, speed, time_offset) and `game::AnimatorSystem`: poses a model's bone
  entities every frame, posts `AnimationEnd` for script-played one-shots; the player's report gains `animation`
  (what plays, ends, a pose fingerprint).
- Placing a model with clips lists its node entities on the root (`"model": {"nodes"}`); older scenes match bones by
  name (said once).
- Ward_Anim (`make_test_characters.py`: Wave, Nod, Glide, Walk, one per interpolation); `tools/samples/add_animation_scene.py`.

**Changed:** the script API's animation functions work on animators as well as sprites, renamed in C++
(`runtime::api::play_animation`, `stop_animation`, `set_animation_speed`, `current_animation`); the Lua and C# names
are unchanged.

**Tests:** unit tests "animation" (6) and "animator" (3); golden `Animation`; `cli.animation_scripts_build_identically`;
`render.animation_cost`.

### 2026-11-20 — Milestone 8C, part 1: skinned meshes
**Added**
- **Skins from glTF** (`asset::ImportedSkin`, `SkinWeights`, `ImportedNode::skin`): joints, inverse bind matrices, the
  strongest four influences renormalised, warnings in plain words; `asset/skinning.h`, the CPU reference.
- **Skinned drawing:** linear blend skinning in the vertex shader (`skinning.hlsli`), shadows too; `MeshData::skin`,
  `DrawItem::skin_palette`; skin data and per-frame palettes read through buffer device addresses (now required, with
  64-bit shader integers).
- Placing a model records each skinned mesh's joints (`"mesh": {"skin", "joints"}`); `SceneView::Mesh` carries their
  world matrices; games and the editor's viewport draw them posed (picking and framing by the posed shape).
- Test characters: CC0 RobotExpressive and SimpleSkin (`tests/data/animation`), and our own **Ward**
  (`tools/samples/make_test_characters.py`); `tools/samples/add_skinning_scene.py`.
- [Animation docs](docs/animation/ANIMATION.md).

**Changed:** ADR-036 accepted with bones as entities (as every glTF node already was); push constants grew to 48 bytes.

**Tests:** unit tests "skinning" (5 cases); golden `Skinning`; `render.skinning_cost`; the editor smoke test's
skinning check.

### 2026-11-20 — Milestone 8B, part 8: the sample and the 8B review
**Added**
- **The 8B sample:** `samples/Hillside/Scenes/Hillside_Storm.noorscene` (`tools/samples/add_hillside_storm.py`,
  through the editor API): a storm rolling into the evening valley (a weather zone faded in by
  `Scripts/storm_roll.luau`), a campfire from the Fire, Smoke and Sparks presets (sparks landing on the hillside), and
  Coth walking round it (`Scripts/coth_circle.luau`, an 8-direction sprite on a character controller). The sample's
  `Assets/Effects` and `Assets/Sprites` (Coth's sheet, with permission).
- `tools/samples/measure_hillside_storm.py`: the sample's cost, ingredient by ingredient, and twenty campfires.
- `game::effect_wind`; `ParticleSystem::Options::wind`.
- **The 8B review** ([M8B_REVIEW.md](docs/milestones/M8B_REVIEW.md)).

**Fixed:** effect particles ignored weather zones' wind (the storm's rain blew, the campfire's smoke didn't); the
particle system read the environment file from disk every fixed step.

**Tests:** golden `Hillside_Storm`; unit test "effect wind".

### 2026-11-20 — Milestone 8B, part 7: gameplay particles
**Added**
- **Particle collisions** (`collide`): the scene's terrains (`game::ParticleGround`, the drawn surface) and planes;
  bounce, friction, radius, die on hit, hit speed. [docs](docs/fx/PARTICLES.md#collisions-sub-emitters-and-budgets-part-7)
- **Sub-emitters** (`sub_emitters`): particles where others hit or die, inline or from an effect asset; drawn, muted
  and soloed with their entity (`EmitterSet::owner`, `fx::sub_emitter_id`).
- **Budgets:** the scene's 100,000 effect particles (`ParticleSystem::Options::max_particles`), shared out in scene
  order; `render.max_distance` (drawing only). The player's report: `particles.hits`, `particles.throttled`.
- NoorEditor: **Collisions** and **Sub-emitters** modules and "Hide beyond" in the Effects panel; the preview collides
  with the scene's terrains.
- Scenes `Particles_Terrain` (a golden) and `Particles_Budget` (50 fountains, measured).

**Tests:** unit tests "particle collisions" (planes, bounce, slopes, terrain), "sub-emitters", "particle budgets";
golden `Particles_Terrain`; `render.particles_cost` measures the 50-emitter budget; the editor smoke test's sub-emitter
check.

### 2026-11-20 — Milestone 8B, part 6: 8-direction characters and scripts
**Added**
- **8-direction sprite sheets:** `"directions"` (the views in the columns, The Uncounted's layout), animations naming
  the first column, `"directional": false` per animation; the view picked from the camera angle
  (`game::sprite_direction`, `game::sprite_frame`). [docs](docs/fx/SPRITES.md#8-direction-characters)
- **Sprite animations from scripts:** Lua `e:play_animation(name, speed)`, `stop_animation`, `set_animation_speed`,
  `animation`, hook `on_animation_end(name)`; C# `PlayAnimation`, `StopAnimation`, `SetAnimationSpeed`, `Animation`,
  `OnAnimationEnd`. `game::SpriteAnimationSystem` posts the end events (`runtime::Event::AnimationEnd`, with a name).
- **Emitter bursts from scripts:** Lua `e:burst(n)`, C# `Burst(n)`, at the next fixed step.
- **World commands** (`World::handle`, `World::command`, `System::attached`): systems offer services to the script API.
- `noor_rt_api` v4 (`sprite`, `sprite_animation`, `burst`, `event_name`).
- NoorEditor: **Directions** and a per-animation **Turns** in the sprite sheet editor; ⟲/⟳ turn the preview.
- noor_player's report: `sprites` (each sprite's animation and start, and how many animations ended).
- **Coth**, from The Uncounted, as the 8-direction test character (`tests/data/render_scenes/Assets/Sprites/coth.png`,
  Tom's art, used with permission).

**Changed:** the editor's viewport animates sprites (and drifts clouds) on the game's clock while playing.

**Fixed:** the sprite docs' example put `margin` and `spacing` outside `grid`, where they aren't read.

**Tests:** unit tests "sprite directions", "sprite scripts", "emitter scripts", "csharp host: sprites through the ABI";
`cli.sprite_scripts_build_identically`; golden `Sprites_Coth`; the editor smoke test's direction check.

### 2026-11-19 — Milestone 8B, part 5: sprites
**Added**
- **Sprite sheets** (`.noorsprite`, importer `sprite`, `asset/sprite_asset.h`, [docs](docs/fx/SPRITES.md)): a texture cut
  into frames by columns and rows or by frame size (margin, spacing), a pivot, pixels per metre, nearest or linear
  filtering, named animations (ranges or lists, frames a second, looping or holding).
- **The `sprite` component** (games and the editor's viewport): an animated, lit, shadow-casting cutout card,
  `upright`/`camera`/`none` billboards, flip, colour, speed and time offset. `game::SpriteLibrary`; `SceneView::sprites`.
- **Nearest filtering** for materials (`MaterialDesc::nearest_filter`): crisp pixel art.
- **Editor API v26:** `noor_doc_sprite` (list, read with the sliced frames, create from a texture, save, apply).
- **NoorEditor: the sprite sheet editor** (a tab): the sheet with numbered frames, grid, world size, pivot, filter,
  animations, an animated preview, Place in scene; **Make sprite sheet** on a texture's settings; Content Browser opens
  and adds sheets. Viewport render stats report `sprites`.
- `tools/samples/make_test_sprites.py`: a pixel-art test sheet (a walker and a coin, drawn pixel by pixel) and scenes.

**Tests:** `sprites:` unit tests (4 cases); `noor_editor_api_tests` "sprite api" (3 cases); goldens `Sprites_Sun`,
`Sprites_Shade`; the editor smoke test's sprite checks.

### 2026-11-19 — Milestone 8B, part 4: the Effects panel
**Added**
- **Six built-in effect presets** (`fx/presets.h`): Fire, Smoke, Sparks, Dust, Rain splash, Magic; never written.
- **Editor API v25:** `noor_effect_presets`, `noor_doc_effect` (list, read, apply onto a new or the selected entity,
  duplicate, reset, save, preview: solo, mute, restart, pause).
- **NoorEditor: the Effects panel** (bottom dock): a gallery of presets and project effects, and a module stack (spawn,
  shape, life and speed, forces, over life with curve and gradient editors, render) saved as you edit; a seed to roll.
- **A live preview in the editor's viewport:** the scene's emitters simulated on the viewport's own clock while editing,
  drawn like a game draws them; the render stats report `particles`, `particle_draws`, `weather_drops`.

**Changed:** `fx::EmitterSet` (emitters by entity) is shared by the runtime system and the editor's preview;
`game::particle_draws` (emitters to draws) by games and the viewport. Render snapshots carry the scene's emitters.

**Tests:** `noor_editor_api_tests` "effect api" (4 cases); the editor smoke test's Effects checks.

### 2026-11-19 — Milestone 8B, part 3: rain, snow and dust
**Added**
- **Weather particles** from the environment: `rain`, `snow`, `dust` (0..1), blown by the ground wind. `weather.hlsl`
  works each drop out from its number, the time and the wind in a box round the camera: no data per drop, no
  simulation; up to 200,000 raindrops (0.18 ms), 60,000 snowflakes, 20,000 motes. Weather zones blend them like fog.
- `tools/samples/add_hillside_weather.py`: Hillside in rain and in snow.

**Changed:** the frame set's shadow map, sky irradiance and aerial volume are visible to vertex shaders too (weather
lights each drop once, per vertex); `rain`, `snow`, `dust` and `wind_speed` blend from 0 in weather zones.

**Tests:** `cli.weather_walk` now checks the rain starts smoothly (trace field `rain`, `noor_trace_check --rain-to`);
`render.particles_cost` holds 200,000 raindrops to 0.5 ms; goldens `Hillside_Rain`, `Hillside_Snow`.

### 2026-11-19 — Milestone 8B, part 2: drawing particles
**Added**
- **Particles are drawn** (games): `render::ParticleDraw` in `FrameDesc`, `shaders/particle.hlsl` (quads from the vertex
  index; camera or velocity facing; flipbooks; a soft dot without a texture), one instanced draw per emitter merged with
  see-through meshes by distance; alpha, additive and premultiplied blending; lit (sun, its shadows, cloud shadows,
  sky), fogged, emission. The emitter's `render` settings ([docs](docs/fx/PARTICLES.md)); `SceneRenderer::particles`.
- Golden test option `FRAMES` (how long a scene runs before its picture).

**Changed:** the frame's shared shader code (uniforms, shadows, environment light, atmosphere, fog) moved from
`mesh.hlsl` to `frame.hlsli`, unchanged (every golden identical).

**Tests:** goldens `Particles_Campfire`, `Particles_Campfire_Night`, `Particles_Glass`; `render.particles_cost`
(campfire +0.005 ms, 10,000 smoke particles +0.58 ms).

### 2026-11-19 — Milestone 8B, part 1: the particle core
**Added**
- **`engine/fx` (NoorFX)**, effect particles (ADR-035, [docs](docs/fx/PARTICLES.md)): the `emitter` component; `.noorfx`
  effect assets (importer `fx`, overrides like environments); rate and bursts, point/sphere/box/cone shapes, lifetimes,
  speeds, gravity, drag towards the wind, size/colour/alpha over life, world or local space, caps. Simulated on the CPU
  at a fixed 60 Hz from a seed: deterministic (own sine/cosine, SplitMix64 streams per emitter).
- **Ground wind** on the environment: `wind_speed` (m/s), `wind_direction` (degrees).
- noor_player's report: `particles` (emitters, alive, fingerprint) and `timing_ms.particles`.
- The 8B plan (eight parts) and ADR-035.

**Fixed:** weather zones blended cloud wind directions the long way round (350 to 10 degrees through 180); any
`*wind_direction` now blends the short way, like `north`.

**Tests:** `particles:` unit tests (maths, shapes, bursts, caps, spaces, the runtime system, a pinned fingerprint equal
in Debug and Release, 10,000 particles in 0.022 ms a step); `cli.particles_build_identically` (a campfire with a
`.noorfx` asset replays bit for bit in a built game); weather blending of wind directions. Particles are not drawn yet.

### 2026-11-19 — Milestone 8A, part 8: the sample and the 8A review
**Added**
- **The 8A sample:** `samples/Hillside/Scenes/Presets/`, the valley under all nine environment presets (applied through
  the editor API by `tools/samples/add_hillside_presets.py`, so the sample's `Assets/Environments` has the nine
  `.noorenv` copies) and `Hillside_Space` (an airless world: a ringed planet, a moon, two suns). Ten goldens.
- `tools/samples/measure_hillside_skies.py`: each sky's GPU cost on the sample (1.1-2.0 ms a whole frame at 1280x720;
  volumetric clouds +0.7 ms).
- **[The 8A gate review](docs/milestones/M8A_REVIEW.md):** the gate passes; exceptions and open questions listed.

**Changed:** `*.noorscene.bak` (an editor save's backup) is ignored, and the sample's stray `Hillside.noorscene.bak`
(committed in Milestone 6) is no longer tracked.

### 2026-11-19 — Milestone 8A, part 7b: clouds with depth
**Added**
- **Volumetric cloud layers** (ADR-034): `"cloud_type": "volume"` and `"cloud_thickness"` (also `cloud2_`). The
  layer's coverage map extruded into a slab (flat bases, rounded tops), carved into lumps by cellular noise, eroded
  by value noise, lit by a march towards the sun (two-lobe phase, Wrenninge's multiple scattering, the two-stream
  diffuse term, the sky above). The look was chosen from rendered variants. Flat layers are unchanged.
- **Reference** in `render/clouds.h` (`cloud_volume_density`, `cloud_volume_march`, tiling `cloud_billow` and
  `cloud_detail`); **GPU** in `shaders/clouds_volume.hlsl`: `CSNoise` bakes the noises into a 128³ tile once,
  `CSMarch` marches at half resolution (`vulkan_clouds.cpp`); the sky pass lays the result over the sky (frame set
  binding 6). FrameUniforms gains `cloud_c` (thickness, volume flag per layer).
- **Editor**: `cloud_type` is a drop-down (layer, volume) in the Inspector.

**Changed:** the sky pass's cloud loop; the environment build check's cloud layer is now a volume.

**Fixed (same day): even cloud edges.** The anti-sparkle fade switched the lumps and erosion off at normal viewing
distances, so every outline was the smooth coverage map. They now fade only when a pixel covers a whole noise cell;
finer, wispier erosion towards the base; a see-through fringe at low densities ("F3", from rendered variants). Golden
`Atmosphere_Volume` updated; 1.51 ms. Better, not cured: round two is in the ideas inbox.

**Tests:** `clouds:` unit tests (shape, column density, sides, horizon coverage, light per unit of cloud, thick
clouds darker, the noises tile), golden `Atmosphere_Volume`, `render.clouds_volume_cost` (1.50 ms measured, budget
2.5), `cli.environment_builds_identically` with a volume; all older goldens unchanged.

### 2026-11-19 — Milestone 8A, part 7: weather zones
**Added**
- **`weather_zone` components** (`game/weather.h`): a box or sphere on its entity with a `blend` distance, a
  `priority` and an environment (a `.noorenv` id or its own settings); blended over the scene's environment by the
  scene camera's position (`game::weather_zone_weight`, smoothstep over the blend distance). A `"weight"` field set by
  a script replaces the distance. `SceneView::weather` reports each zone's weight.
- **`asset::blend_environments`**: numbers linearly (time of day, moon phase, day of year and north the short way
  round), colours in linear light, sky modes, switches and seeds at the halfway point; a field one side lacks goes
  to its neutral value (no fog, no cloud, turbidity 1...) or keeps the other side's.
- **`Atmosphere::Bracket` and `Atmosphere::prewarm`**: CPU-side atmosphere answers for settings that move every frame,
  from cached atmospheres on a coarse grid (interpolated by turbidity), built in parallel up front. Exact on grid
  points (every preset). The cache holds 64 atmospheres instead of 16.
- **noor_player `--trace-environment`**: fog, exposure, camera and zone weights per frame in the report;
  `noor_trace_check` judges a trace (smooth, monotone, the same in two runs, time spent gathering the scene).
- **Editor**: Create ▸ Weather zone (a fog bank); the Inspector picks a zone's environment (a project environment, or
  a copy of a preset).

**Fixed (before release):** blending rebuilt the CPU atmosphere's tables every frame (0.24 s each: 20 FPS); a zone
fully on replaced the whole environment instead of only what it sets (a fog-only zone turned the atmosphere sky off).

**Tests:** `weather:` unit tests (blending rules, zone weights, the view blending by camera position, Luau scripts
rolling a storm in and running the clock), `atmosphere: a blend's settings are answered from two cached grid
atmospheres, smoothly`, `cli.weather_walk` (a built game's camera walks into a fog bank: 540 frames smooth, identical
to the project, gathering the scene under 8 ms at p95 and never 120 ms), the editor smoke test's weather zone checks.

### 2026-11-18 — Milestone 8A, part 6: environment assets and presets
**Added**
- **`.noorenv` environment assets** (`asset/environment_asset.h`): every environment setting in a file (importer
  `environment`); a scene's environment component `{"asset": id, ...overrides}`; an environment's `ev100` (number or
  "auto") wins over the camera's.
- **Nine built-in presets**: Clear Noon, Fair Weather, Golden Evening, Dusk, Overcast, Foggy Dawn, Pink Mist, Moonlit Night, Stormy Coast (never written; applied as project copies that remember their preset). Tuned
  by rendering three variants of each on Hillside; where more than one was good, both shipped.
- **Editor API v24**: `noor_environment_presets`, `noor_doc_environment` (list, read, apply, duplicate, reset, save).
- **Editor**: the **Environments** tab (gallery with pictures, Apply, Duplicate, Edit, Reset);
  `tools/samples/render_environment_thumbnails.py` renders the presets' pictures from the engine.

**Changed:** clouds let light through by their thickness (`render::cloud_diffuse_transmittance`, the two-stream
approximation), in their shading and in automatic exposure (overcast scenes were metered as if the blocked sunlight
vanished); golden `Atmosphere_Clouds` updated.

**Tests:** `noor_editor_api_tests` "environment api" (apply changes sky, sun, fog and exposure in one undo step;
duplicate/edit/reset leave the presets alone; save and reopen), `cli.environment_builds_identically` (pixel-identical
in a built game), the editor smoke test's gallery checks, `cloud_diffuse_transmittance`.

### 2026-11-18 — Milestone 8A, part 5: night and space
**Added**
- **The moon** (`asset::lunar_direction`, `moon_illuminance`, `default_moon_phase`): drawn as a lit sphere, its
  light from the same model (0.16 lux full), the shadow light when it outshines the sun, its glow in the live sky.
  Environment fields `moon`, `moon_phase`.
- **Stars** (`render/stars.h`, `shaders/stars.hlsli`): a deterministic field with the real counts by magnitude,
  colours by temperature, turning about the celestial pole; `stars`, `star_brightness`, `star_seed`.
- **`sky: "space"`**, **celestial bodies** (`celestial_body` component: angular size, albedo, rings with tilt;
  `render::CelestialBody`, up to 8), **extra suns** (directional lights after the first: `LocalLight::Type::Directional`).
- **Automatic exposure**: `ev100: "auto"` (`render/exposure.h`: an incident meter with a night bias);
  `asset::Atmosphere::sky_illuminance`. The player's report has `ev100`.
- Hillside_Night (`tools/samples/add_hillside_skies.py`, renamed from add_hillside_haze.py, now writes both
  scenes); render scene `Space`.

**Changed:** the live sky's images are scaled (`live_sky_scale`) so half floats keep precision at night (the
night sky had turned into bands); the sun's disc is drawn from `Lighting::sky` (the real sun, even when the moon
is the light).

**Fixed (on the way):** the night bias first *added* exposure (midnight looked like noon); its test now checks
that a darker scene never gives a brighter picture.

**Tests:** the moon against astronomy (full opposite the sun, winter full moons higher, a half moon 1/pi of a full
one), the star field against the real counts, the sky turning about the pole, exposure (sunny 16, the night bias,
monotone pictures), skylight by sun height; goldens `Hillside_Night`, `Space`.

### 2026-11-18 — Milestone 8A, part 4: layered clouds
**Added**
- **Cloud layers** (`noor/render/clouds.h`, `shaders/clouds.hlsli`; `render::Lighting::clouds`, `Lighting::time`):
  up to two, deterministic from a seed, drifting with game time; lit by the sun and sky at their altitude (a new
  compute step, `CSCloudLight`); drawn sharp in the sky pass; in the live sky's lighting (overcast lights the scene
  grey); shading the sun on the ground (drifting cloud shadows). Environment fields `cloud_*` and `cloud2_*`; the
  editor's **+ Clouds** button. noor_player and the editor viewport pass their time.
- The live sky keeps a cloud-free background image (what the sky pass draws under its clouds); frame set binding 5
  (the clouds' light).
- The player's timing report has `min` values.

**Changed:** the GPU timing tests (`render.day_cycle`, `render.clouds_cost`) judge each run's fastest frame and print
medians: on a working desktop the GPU idles at 210 MHz and is shared, and medians swing 3-10x between runs.

**Measured:** clouds +26-28 µs a frame; a lighting rebuild 0.08 ms every few seconds while they drift.

**Tests:** cloud maths (the same seed gives the same sky, coverage from clear to overcast, the wind's drift, detail
fading with distance, overcast dimming the sun to e^-density); goldens `Atmosphere_Clouds`, `Atmosphere_Overcast`;
`render.clouds_cost`.

### 2026-11-18 — Milestone 8A, part 3: fog and aerial perspective
**Added**
- **Height fog** (`noor/render/fog.h`, `shaders/fog.hlsli`; `render::Lighting::fog`): full density below a
  height, thinning above; closed-form optical depth; lit by the sun (Henyey-Greenstein) and the sky's average
  radiance; on opaque and see-through surfaces and the sky. Environment fields `fog_density`, `fog_height`,
  `fog_falloff`, `fog_anisotropy`, `fog_color`; the editor's **+ Fog** button.
- **Aerial perspective** with the atmosphere sky: a 32³ froxel volume built every frame by compute (`CSAerial`),
  `asset::Atmosphere::aerial_perspective` as its reference; `aerial_scale` (`AtmosphereDesc::aerial_distance_scale`).
- **Hillside_Haze**: a second scene of the Hillside sample (`tools/samples/add_hillside_haze.py`) and its golden.
- Shaders can share code: `.hlsli` files next to them are build dependencies.

**Changed:** `noor_cli cook`/`build` cook a CSG model shared by several scenes once; one whose brushes differ
between scenes under the same entity id is refused with an explanation (it used to be overwritten silently).

**Measured:** the aerial volume 0.025 ms a frame; Hillside_Haze 1.33 ms GPU a frame at 1280×720.

**Tests:** fog maths against numerical integration (cameras inside and above the fog), density, height,
falloff, the horizon, the phase function's normalisation, the seam (far ground and sky reach the same
colour); aerial perspective against the sky; fog fields reaching the view; `cli.csg_shared_between_scenes`;
golden `Hillside_Haze`.

### 2026-11-18 — Milestone 8A, part 2: the live sky on the GPU, time of day
**Added**
- **The renderer's first compute shaders** (`shaders/atmosphere.hlsl`, `vulkan_atmosphere.cpp`):
  `Renderer::set_atmosphere` builds the atmosphere's tables, the sky image and its pyramid, the prefiltered
  environment cube and its irradiance on the GPU, a port of the CPU reference; rebuilt in the frame the sun
  moves (0.16 ms), one copy per frame in flight. A GPU pass `Environment` in the frame stats.
- **`time_of_day`** on the environment component (`latitude`, `day_of_year`, `north`): places the sun
  (`asset::solar_direction`); the editor's **+ Time of day** button.
- `noor_player --cpu-sky` (the CPU reference path, for comparisons); the report's `sky_builds`, the
  timing's `environment_gpu` and `max` values.

**Changed:** the environment's irradiance (SH9) moved from the frame uniforms to a storage buffer (frame set
binding 3), written by `set_environment` or the live sky.

**Measured:** GPU path vs part 1's CPU goldens: mean difference 0.02 / 0.04 of 255. A day from 5:00 to
21:00 rebuilt every frame at 60 FPS (focused window). See MEASUREMENTS.md for the wall-time caveat.

**Fixed (on the way):** a shared sky copy made each frame's compute wait for the previous frame's
presentation (31 ms frames); one copy per frame in flight.

**Tests:** `render.day_cycle`, `render.golden_Atmosphere_{Noon,Sunset}_cpu_sky`, solar positions against
textbook values (equinox, solstices, hemispheres), `time_of_day` in a scene.

### 2026-11-18 — Milestone 8A, part 1: the atmosphere sky
**Added**
- **`asset::Atmosphere`** (ADR-033, proposed): a physically based atmosphere (Rayleigh, Mie, ozone over a
  spherical planet; Hillaire 2020's coefficients): transmittance and multiple-scattering tables, the sky by
  ray marching, the sun's transmittance, an equirect sky image. CPU reference implementation; the GPU port
  is part 2.
- **`"sky": "atmosphere"`** on the `environment` component (`turbidity`, `ground_albedo`, `altitude`): the
  sky follows the sun light; the sun's colour is its own (white unless given) times the air's
  transmittance; the sky's intensity is the sun's illuminance. `"sky": "gradient"` forces the procedural sky.
- **The sun's disc** in the sky (`render::Lighting::sun_disc`, 0.53°, limb-darkened), on for the atmosphere.
- **Editor:** a **sky** dropdown on the environment component (hdri, gradient, atmosphere).
- Goldens `Atmosphere_Noon`, `Atmosphere_Sunset`.

**Measured:** tables about 0.1 s once per setting; a sun change rebuilds the sky and its lighting in about
120 ms (CPU); the scene pass with the sky 0.09 ms at 1280×720 (RTX 3080).

**Tests:** `test_atmosphere.cpp`: transmittance against the hand-worked optical depth and the published
values, Chapman's horizon air mass, the table against exact marches, the sun's colour at 2° and 80°, the
zenith's luminance against the CIE clear-sky formula, ozone's effect on the twilight zenith, haze and
altitude, the sky image's layout, and a scene's environment reaching the view.

## Milestone 7 complete (procedural graph and first generation tools)

### 2026-11-18 — After the Milestone 7 review: rocks and grass from NoorMD
**Added**
- **Rocks:** NoorForm rocks convert to the NoorEngine layout (collision pieces as `extras.noor` blocks, part
  `detail`); fixture `tests/data/noorform_rock` (Cracked rock 01, three levels, 0.9 MB).
- **Convex collision** (`"collision": "convex"` / collider shape `convex`): the convex hull of a collision
  node's points, for placed models (static or moving) and scattered instances (`scatter_shapes`, which
  replaces `scatter_capsules`).
- **Plants:** grass packs convert as `vegetation.plant` (no trunk capsule); one folder may hold several
  single exports.
- **Scatter:** `draw_distance` and `shadows` fields; placements cached between frames and shared with the
  renderer and the editor (`game::place_scatter_cached`, `ScatterPlacement`).
- **Hillside:** 24 rocks (on the painted rock and in the meadow) and 20,179 grass clumps (a `Grass` scatter:
  45 m draw distance, no shadows). Golden and fingerprint updated.

**Fixed (performance):** a scatter's ground (`terrain::Heights::shared`, weak references only) was rebuilt
every frame in games (25 ms with grass); collecting a scatter copied every instance's matrix each frame (50 ms
in Debug for 20,000). Now 2.8 ms in Release, 1.2 ms in Debug.

**Tests:** rock import and convex collision (placed and scattered), the placement cache keeping its ground,
`gate.hillside_grass_generate_equals_cook`.

### 2026-11-18 — Milestone 7, part 8: Hillside's forest and the Milestone 7 review
**Added**
- **`paint_mask` node**: a terrain paint layer's weight for one material as a mask.
- **Trunk collision for scattered trees**: each model's capsule collision node becomes a static capsule
  per instance in games and Play (`PhysicsSystem::Options::scatter_capsules`); drawing and physics share one
  placement (`game::place_scatter`), so the trunks are where the trees are.
- **Hillside grows a forest**: 142 oaks (NoorForm Oak 01, 04, 07, converted to the export layout) placed by
  `Assets/Graphs/forest.noorgraph` on the painted meadow, off the path, out of the house's yard and the
  camera's view; `tools/samples/add_hillside_forest.py` adds it through the editor API. Golden updated.
- **Milestone 7 review** (`docs/milestones/M7_REVIEW.md`): the gate passes; ADR-014 and ADR-032 accepted.

**Fixed:** `noor_cli generate` couldn't find assets (no resolver), so graphs reading an asset (paint mask,
heightmap) failed on the command line only.

**Tests:** trunk collision in a game (rays hit trunks, the ground beside them), `cli.cook_hillside`,
`cli.generate_hillside_forest` (pinned; Debug = Release), `gate.hillside_forest_generate_equals_cook`;
the Hillside golden and the built game's walk run on the forest.

### 2026-11-18 — Milestone 7, part 7: scatter
**Added**
- Graph nodes **`scatter`** (blue-noise placement: one candidate per grid cell from its own random stream,
  density × mask, minimum spacing, random turn and size, stable ids; on a height field or a flat area),
  **`exclude`** (a mask that keeps paths and circles clear) and **`merge`** (two instance sets, the second
  kept away from the first). Deterministic turns (sine/cosine from + − ×). Debug and Release write the
  same bytes (fingerprint pinned).
- **Instance set files** (`noor/asset/instance_set.h`, `<output>.instances.json`): plain JSON, one row per
  instance, bit-exact; written by `noor_cli generate` and cooking. Proposed for Noor Interchange (v0.5 draft).
- **`scatter` component** (`noor/game/scatter.h`): draws a graph's cooked instance set with levels of
  detail and instancing (no entities per tree), standing on a terrain if it names one; a hand-made layer
  of `removed` ids and `added` instances that regeneration never touches.
- **Editor:** Create ▸ Scatter; the scatter card (graph, output and ground pickers, Plant and Remove brushes,
  radius, the model to plant); instance-set previews (dots from above) and counts in the graph editor.
  Editor API **v23**: `noor_doc_scatter_edit` (strokes, one undo step each).
- Cooking (editor Save, `noor_cli cook`/`build`) covers every graph a scene uses: terrain bases and scatter
  components (`game::graphs_used`). CLI messages say "graph(s)".
- **A standalone reader** (`tests/tools/instance_reader.cpp`): reads the file with only a JSON parser.
- Test data: `tests/data/forest_project` gains a graph making hills and a forest, and `Scenes/Scattered`.

**Not yet:** trunk collision for scattered trees; moving one tree; masks from paint layers (Hillside's
meadow, part 8).

**Tests:** six unit tests (determinism, spacing, masks, ids; density near and under the packing limit;
merge; mistakes on the node; the file round trip; hand-made edits surviving two reseeds; graphs a scene
needs), an API test (remove, undo, plant along a drag, reseed keeps both), `cli.cook_forest`,
`cli.generate_forest` (pinned fingerprint), `tools.instance_reader`, `render.golden_Scattered`,
`render.scatter_check`, four editor smoke checks.

### 2026-11-18 — Milestone 7, part 6: instanced drawing and levels of detail
**Added**
- **Instanced drawing** (ADR-032, `render/src/vulkan_instancing.cpp`): each frame the renderer culls draw
  items against the camera and batches the ones that share a mesh range, material and pipeline into one
  instanced draw; matrices and tints come from a per-frame instance buffer (vertex binding 1). Shadows
  are culled and batched per cascade. Callers don't change. Push constants 96 → 32 bytes.
  `FrameStats` gains `instances` and `culled`.
- **Levels of detail** (`noor/game/model_lod.h`): a placed model switches as a whole between its
  manifest's levels by camera distance (scaled for the field of view and the model's size), in the
  player and the editor viewport. The scene view records each mesh's model root and the placed models.
- **Cutout mipmaps keep their coverage** (`TextureSettings::alpha_coverage`, used for cutout base colours
  in models), so foliage doesn't thin out with distance.
- `Scene::world_matrices()`: every entity's world matrix in one pass.
- `noor_player --report` gains `timing_ms` (wall, game, view, collect, record, GPU: median and p95) and
  the render stats `instances` and `culled`.
- `tools/samples/build_forest.py` and the test forest `tests/data/forest_project` (120 oaks).

**Fixed (performance):** collecting a scene's view was quadratic (each entity's parents looked up one by
one): 189 ms a frame for 10,000 entities, now about 6 ms; the editor's snapshots use the one-pass
matrices too. Asset paths are resolved once per frame instead of once per mesh.

**Measured** (`docs/rendering/MEASUREMENTS.md`): a 2,000-oak forest went from 5 FPS (192 ms; GPU 22 ms,
4,784 draws, 30.5 M triangles) to 60 FPS (vsync; CPU about 9 ms, GPU 2.5 ms, 24 draws, 0.43 M triangles).

**Not yet:** level cross-fades and hysteresis; persistent instance buffers; GPU culling. Forests of
entities cost CPU (6 ms for 10,000 entities): part 7's instance sets are the fix.

**Tests:** five unit tests (levels from the manifest and chosen by distance/view/size, no switching
without a usable manifest, model roots in the scene view, one-pass world matrices, coverage-keeping
mips), `render.golden_Forest`, `render.forest_instancing_lod` (batching, culling, LOD numbers, Vulkan
validation), an editor smoke check (the viewport switches the oak's level). Smoke checks that counted
triangles now account for culling.

### 2026-11-18 — Milestone 7, part 5: generated assets in (Noor Interchange)
**Added**
- **Manifest reader** (`noor/asset/interchange.h`): reads a `<name>.noor.json` (v0.4 and older) into the
  generator, recipe and seed (unsigned 64-bit, never a float), bounds, files, levels of detail with their
  `max_distance` and billboard flag, collision, family, wind (kept, unused), tags, licence, validation,
  provenance and the tool's own block. Every mistake is an issue on its field: a float or negative seed,
  the wrong space, a path out of the folder, gaps in the levels, distances that don't grow, missing or
  changed files (SHA-256), a newer major version.
- **Level checks** (`check_levels`): every level file opened and compared with the manifest (triangles,
  the root node's level / distance / billboard, collision only in level 0).
- **Content Browser ▸ Details**: what an asset is; for a Noor Interchange asset, where it came from
  (generator, recipe, seed, family), each level (triangles, size, distance, billboard), collision, wind,
  licence and any problems. Editor API **v22**: `noor_doc_asset_details` (cached until the files change).
- **`noor_cli info`** prints a manifest and checks every level; **`noor_cli validate`** checks every
  manifest in a project (errors fail it, warnings are printed).
- **`tools/interchange/noormd_engine_export.py`**: converts a NoorForm pack into the NoorEngine export
  layout (the guide's rules: names, `extras.noor`, parts, a trunk capsule, JPEG and smaller far-level
  maps, a UUID v5 id, a v0.4 manifest), until NoorMD writes it itself. Outside the engine.
- **Test fixture** `tests/data/noorform_oak`: Oak 01 (four levels, 6.5 MB) converted from a NoorMD variety pack.

**Fixed:** a collision node's `radius`, `height` and `size` (Noor Interchange 3.5) now reach the collider
when a model is placed; a trunk capsule used to get the default 0.3 m × 1.8 m.

**Found (for NoorMD, in the export guide's section 8):** the oak's trunk stands about 1.1 m from its
pivot; geometry is now half of the file size (quantization would help); the guide's 4 MB per oak is
really about 6.5 MB.

**Not yet:** switching levels by distance (part 6: today level 0 is drawn at every distance); `convex`
collision in the physics (rocks); a live reload when only a `.lodN.glb` changes.

**Tests:** four unit tests (one asset with four levels, the capsule, mistakes as issues, an older
manifest), `cli.info_interchange`, an API test (details, a level deleted behind the editor's back) and three
editor smoke checks (one asset, the Details pane, placed and drawn with its capsule).

### 2026-11-18 — Milestone 7, part 4: the graph editor
**Added**
- **Graph editor** (`Panels/GraphPanel.cs`): a tab for `.noorgraph` files (Content Browser double-click,
  File ▸ New Procedural Graph…): nodes on a canvas with typed coloured pins and wires, add / move / link /
  delete, a side panel for parameters, graph outputs and the graph's seed, runs in the background after
  each change (Run, Cancel, progress), per-node results (time, cached, tiles) and previews (thumbnails and
  a 256 px view), issues on their nodes, Ctrl+Z / Ctrl+Y, saved at once in the engine's file format.
- Editor API **v21**: `noor_graph_node_types`, `noor_graph_check` (issues + canonical text), per-node
  results and `written` in the run status, `noor_doc_generate_preview`. Graphs without outputs run too.
- Background runs resolve asset files from a snapshot taken when they start (a Rescan can't race them).

**Changed:** graph edits are file edits with the tab's own undo (like the material editor), not scene
commands as the plan said (ADR-014 part 4 notes say why).

**Tests:** an API test (node types, check, per-node results, previews); 5 editor smoke checks (run, add and
link a node and make it an output, only dependants rerun after a change, an issue on an unconnected node,
undo saved).

### 2026-11-18 — Milestone 7, part 3: terrain as a graph client
**Added**
- **Tiles as regions**: tile-local field nodes (`run_tile`) are keyed, cached and recomputed tile by
  tile; nodes with bounds (`touches`) pass untouched tiles through under the input's keys. Per-node tile
  counts (run, cached, passed). Field shapes are planned before running.
- Nodes: **`stamp`** (hill/plateau with bounds), **`terrace`**, **`height_band`** (mask), **`slope`**
  (mask; deterministic cosine for its thresholds), **`heightmap`** (a picture asset; its key includes the
  file's size and time). noise, combine, clamp and remap became tile-local (same bytes: the pinned
  fingerprint didn't move).
- **Terrains on graphs**: `"base": "graph"` (`graph`, `graph_output`); sculpting and paint stay on top
  through a new seed. Games read the field cooked in `.noor/cooked/graphs/<id>/`, written by the editor
  (when the graph changes, and on Save) and by `noor_cli build`/`cook` (`procgraph::cook`).
  `game::terrain_settings`, `game::cooked_graph_folder`; `collect_view` takes the project root.
- `generate` doesn't rewrite files whose bytes wouldn't change.

**Fixed (during the part):** a patch script that opened `terrain.cpp` for writing before reading it
emptied the file; restored from git before anything was built or committed.

**Tests:** 3 more `test_procgraph` cases (moving a stamp in one corner recomputes exactly that tile in the
stamp and downstream, the other three bit-identical with unchanged keys; terrace/height band/slope;
heightmap equals the terrain's reading), an API test (a graph-based terrain cooks itself, a sculpted bump
survives a new seed), `cli.build_graph_terrain` + `player.graph_terrain` (a built game: a walker lands on
the cooked terrain, 2 mm from its height).

### 2026-11-18 — Milestone 7, part 2: cache, jobs and noor_cli generate
**Added**
- **Results cache** (`procgraph::Cache`): node results under their keys, in memory and in
  `<project>/.noor/cache/procgraph/<key>.npgc` (atomic writes, complete results only, damaged files
  recomputed). Only changed nodes and their dependants run again.
- **Worker threads** with a progress callback and cooperative **Cancel** (`ErrorCode::Cancelled`, new);
  results and reported order independent of threads.
- **`procgraph::generate`** and **`noor_cli generate <project> <graph> [--out] [--threads] [--no-cache]`**:
  fields as `.noorfield` files (a documented little binary format), a `<graph>.fingerprint.json` with
  what made them; nothing written unless every output was made.
- Editor API **v20**: `noor_doc_generate_start`, `noor_doc_generate_status`, `noor_doc_generate_cancel`
  (a background run in the document; closing it cancels and waits).
- Value encoding (`encode`/`decode`), `save_field`/`load_field`.

**Tests:** 5 more `test_procgraph` cases (bytes round trip; 3 run + 3 cached after one edit, from memory
and from files; a damaged cache file; 1 thread vs 8 identical; cancel writes nothing and caches only
finished nodes; generate byte-identical and readable), `cli.generate` (the pinned fingerprint from the
command line), an API background-run test (cancel, progress, cache) and `api.generate_matches_cli` (the
editor's files equal the CLI's).

### 2026-11-18 — Milestone 7, part 1: procedural graph core
**Added**
- **`noor_procgraph`** (editor/tools only; the runtime-boundary test forbids it in games;
  [docs/procgraph/PROCGRAPH.md](docs/procgraph/PROCGRAPH.md)): `.noorgraph` files (format 1, deterministic
  save, unknown fields kept), typed pins with units (field in metres = height field, in ratio = mask),
  a node registry with versions and parameter schemas, validation with every mistake on its node,
  deterministic evaluation (dependency order, ties by id) with per-node seed streams, keys and a trace
  (ms and bytes per node).
- Built-in nodes: `constant`, `math`, `noise` (the terrain's noise, bit-identical), `combine`, `clamp`, `remap`.
- `.noorgraph` assets (importer `procgraph`); the editor explains them when added to a scene.

**Tests:** `test_procgraph` (6 cases): file round trip (seeds above 2^63 included), evaluation order,
a pinned fingerprint (Debug = Release), node randomness independent of other nodes, noise identical to
the terrain's, keys that change exactly downstream of an edit, every kind of mistake reported on the
right node while the healthy rest still runs, failures while running skipping their dependants.

### 2026-11-18 — Noor Interchange v0.4 and NoorMD's NoorEngine export mode
- [docs/interchange/NOORMD_NOORENGINE_EXPORT.md](docs/interchange/NOORMD_NOORENGINE_EXPORT.md): what a new
  "NoorEngine" export mode in NoorMD should write for trees and rocks (Tom's decision: NoorMD adapts
  to the engine), checked against NoorMD `4905b22` and a real nine-oak pack: Interchange file names,
  one `.noor.json` per asset with a stable UUID v5 id (NoorForm namespace fixed in the guide),
  `extras.noor` blocks, collision flags, JPEG textures (three quarters of today's 170 MB pack is
  duplicated PNG textures), a checklist.
- **Noor Interchange v0.4** (optional additions, v0.3 files stay valid): `max_distance` and `billboard`
  for LOD levels, `family` for variety packs, `wind` channels (never in `COLOR_0`), the vegetation pivot.
  Schema updated. NoorEngine's own CSG export still writes v0.3 (it uses none of the new fields).
- M7 plan: two of the three open questions answered (export mode; assets go in the repository if they fit).

### 2026-11-18 — Milestone 7 plan: procedural graph and first generation tools
- [docs/milestones/M7_PLAN.md](docs/milestones/M7_PLAN.md): eight parts toward the gate (reproducible
  outputs, overrides that survive regeneration, region-limited invalidation, outputs used outside the
  generator), risks first (determinism with threads and caches), scope listed as later work, three
  open questions for Tom.
- [ADR-014](docs/decisions/ADR-014-procedural-graph.md) (Proposed): `.noorgraph` assets, typed
  registered nodes, pure evaluation with a content-addressed cache (per tile for tiled data), override
  layers, headless `noor_cli generate`, games load outputs.
- Trees and rocks stay NoorMD's job: NoorEngine imports them as files and places them (scatter,
  instancing and LOD are engine work); ADR-007 (plugins) stays open.
- **Fixed:** README's status headline still said "Milestone 4 in progress" and listed editor API v9;
  it now says Milestones 0–6 are done (with links to every review) and API v19.

### 2026-11-18 — Milestone 6, part 8: the Hillside sample and the gate review
**Added**
- **`samples/Hillside`**: a sculpted, painted landscape (meadow, rock, a dirt path) and a brick
  house from brushes (doorway and windows cut out, plank floor and roof), sun and HDRI sky, and a
  walker that follows the path into the house. Built by **`tools/samples/build_hillside.py`**
  through the editor API (ctypes), the same calls the editor's tools make; rerunnable.
- **`noor_doc_terrain_height`** (editor API **v19**): a terrain's ground height at a world point,
  including unsaved sculpting.
- CC0 texture sets from Poly Haven for the sample: Leafy Grass, Rock Face, Dry Ground 01 (1K).
- [Milestone 6 review](docs/milestones/M6_REVIEW.md): the gate passes, with seven tracked exceptions.

**Fixed:** the Render panel smoke check read the GPU pass timings once after a fixed delay and
flaked when they arrived a frame late; it now waits for them (up to 5 s).

**Tests:** `render.golden_Hillside`; `cli.build_hillside` + `player.hillside_walk` (the walker ends
inside the house, on its floor); an API test that opens a copy of the sample and exports its house
as a GLB that reads back with its collision node and triangles; a terrain-height API test.

### 2026-11-18 — Milestone 6, part 7: painting
**Added**
- **Terrain painting**: up to four material layers per terrain (`"layers": [{"material", "tiling"}]`),
  blended by painted weights (`terrain::PaintLayer`, `.noorpaint` asset, importer `terrain_paint`).
  Inspector ▸ terrain ▸ **Paint** with a material; painting a new material adds it as a layer. One undo
  step per stroke; Save writes the weights; games load them. **Material layers** list with tiling.
- **Splat rendering**: `render::DrawItem::splat` (+ `splat_layers`, `splat_tiling`): the mesh shader
  blends four materials (colour, normal, roughness, metalness, occlusion, glow) by the weights in the
  vertex colour. Terrains are always drawn as splats. `MeshPushConstants` grew to 96 bytes.
- **Paint on CSG faces with the same tool**: with Paint on, a click on a brush face gives it the material.
- `terrain_edit::paint`, `mix_weights`; `TileLayer<T>` shares the sparse tile storage of sculpt and paint.
- Editor API **v18**: `noor_doc_sculpt` takes `"tool": "paint"` and `"material"`.

**Changed:** fast strokes are filled in with dabs along the pointer's path (sculpting too); the
document keeps one edit per terrain entity with both layers.

**Fixed:** a smoke check's upper bound on a sculpted height depended on frame timing (it failed at
7.03 m against 7 m); it now checks "a few metres".

**Tests:** paint unit tests, an API painting test (layers, the four-material limit, undo, saved
weights), a fast-stroke API test, `render.golden_TerrainPaint`, smoke checks for terrain and face painting.

### 2026-11-18 — Milestone 6, part 6: terrain sculpting
**Added**
- **Sculpting**: Raise, Lower, Smooth and Flatten brushes with radius, strength and falloff
  (`noor_terrain_edit`, editor/tools only). Inspector ▸ terrain ▸ **Sculpt**; a circle on the ground
  shows the brush; left-drag sculpts. A stroke is one undo step (region undo: the touched rectangle).
- **Sculpt layer** (`terrain::SculptLayer`, `.noorsculpt` asset, importer `terrain_sculpt`): sparse
  height offsets on top of the generated base, so a new seed or heightmap keeps the manual edits. The
  first stroke creates `Assets/Terrain/<name>.noorsculpt`; Save writes it; games load it.
- `terrain::raycast` (where a ray meets the ground), `Heights::compose`/`recompose`.
- Play collides with unsaved sculpting (`game::WorldOptions::terrain_heights`).
- Editor API **v17**: `noor_doc_sculpt`, `noor_viewport_set_brush`, "stroke" viewport events.
- The viewport rebuilds only the terrain tiles that changed.

**Fixed:** the editor's ground grid z-fought with flat terrains at 0 m: the grid is hidden when the
scene has a terrain (the terrain is the ground).

**Tests:** `test_sculpt` (6 cases); an editor API test (stroke → undo/redo → save → new base keeps the
hill → Play collides with unsaved sculpting); `cli.build_sculpt` + `player.sculpted_hill` (a built
game walks over a hill that exists only in the sculpt layer: feet 8 mm from the exact height);
editor smoke checks driven by a test pointer. The runtime-boundary test now also forbids
`noor_terrain_edit` in games.

### 2026-11-18 — Milestone 6, part 5: terrain foundation
**Added**
- **`noor_terrain`** (runtime-safe module, [docs/terrain/TERRAIN.md](docs/terrain/TERRAIN.md)):
  heightfield tiles of 256 × 256 cells (257² samples, shared edges, no seams), heights from
  deterministic gradient noise (seed, height, scale, octaves), a 16-bit or 8-bit PNG heightmap
  (min/max metres) or flat; `Heights::height`/`normal`, tile meshes, a shared cache so the renderer
  and physics generate once.
- **`terrain` component**: the settings above plus `material` and `collision`; centred on its
  entity (rotation and scale ignored). Drawn in the editor viewport, Play and built games; one static
  Jolt `HeightFieldShape` per tile. The mesh triangles use Jolt's diagonal, so you stand on what you
  see.
- Editor: **Create ▸ Terrain**; the Inspector shows `base` as a list and the heightmap as a picture
  picker.
- [ADR-031](docs/decisions/ADR-031-terrain-heightfields.md) **accepted**, with what part 5 changed
  (settings in the component until there are layers to store; centred frame; measured cost).

**Tests:** `test_terrain` (same seed → same hash, pinned for seed 7 and identical in Debug and
Release; seams; height lookup vs. mesh triangles; heightmap; settings; cache); `golden_Terrain`;
`cli.build_terrain` + `player.terrain_walk` (a built game: a character walks up a heightmap ramp,
feet within 2 mm of the ground); 4 editor smoke checks. A configure-time check now fails the build
when a doctest name contains `;` (ctest splits names there and silently ran nothing for four of our
tests until we noticed).

### 2026-11-18 — Milestone 6, part 4: CSG in games (cook, collide, export)
**Added**
- **Cooked CSG models** (`noor/game/cooked_csg.h`): `<project>/.noor/cooked/csg/<model>.ncsg` with
  triangles, one submesh per material id, and the key of the brushes (`game::csg_model_key`,
  FNV-1a over the brushes' data). The editor cooks on Save and Play; `noor_cli build` cooks into the
  game; **`noor_cli cook <project>`**. noor_player and Play draw cooked models and report missing
  or stale ones.
- `csg::brushes_from_scene` and `csg::cook_model` (`noor/csg/scene_csg.h`): one reader of brush
  JSON for the editor and the tools.
- **Collision:** mesh colliders on CSG models use their cooked triangles
  (`PhysicsSystem::Options::csg_triangles`); new models get `"collider": {"shape": "mesh"}`; the
  Inspector shows the cost.
- **Export GLB…** (`asset::export_noor_glb`, Noor Interchange v0.3): GLB with PBR materials,
  embedded PNG/JPEG maps, baked material UV transforms, tangents, a `-colonly` collision node, and a
  `.noor.json` manifest. Editor API **v16**: `noor_doc_export_csg`.

**Fixed:** CSG model entities were skipped by `collect_view` (it skipped entities with none of the
components it drew); a new member of `SceneRenderer` was declared above the material library and
emptied its resolver (the render goldens caught it: textured materials lost their maps).

**Tests:** `cli.build_csg` + `player.csg_doorway` (a built game: one walker through an arched
doorway, the other stopped by the wall, the wall drawn, validation clean); unit tests for cooking
(round trip, damaged file, key) and export (re-imported: collision node, embedded image, triangle
count, manifest fields and hash); 2 editor smoke checks (save cooks, Export writes GLB + manifest).

### 2026-11-17 — Milestone 6, part 3: surface materials and UVs
**Added**
- Per-face surfaces on brushes: `"faces": [null | {material, uv_scale, uv_offset, uv_rotation,
  uv_align}]`; `uv_align` "world" (default) or "brush" (`csg::Align::Brush`: textures turn with a
  rotated brush).
- Viewport: a click on a CSG model reports the brush face (`"face"` in click events); the picked
  face is outlined in cyan (`noor_viewport_set_face_highlight`). Editor API **v15**.
- Inspector: a **Faces** section on brushes (the picked face first and open): material per face,
  UV repeats/offset/rotation/alignment, texel density, "Use the brush's".
- `noor_doc_csg` lists each brush face's material and triangle count.

**Tests:** a unit test (a face with its own material, brush-aligned UVs on a rotated wall); 3 editor
smoke checks (two real clicks pick the front face, it takes a material while the top doesn't, a
window cut afterwards keeps the front's material and doesn't spread it).

**Not yet:** drag and drop of materials onto faces, triplanar projection.

### 2026-11-17 — Milestone 6, part 2: brushes in the editor
**Added**
- **Create ▸ Brush** (box, wedge, cylinder, cone, sphere, stairs, arch): into the selected CSG
  model, or a new **CSG model** entity. Brushes are entities with a `brush` component; the
  model's child order is the order of operations.
- Inspector: shape and operation from lists, a material picker (any component's `material` field),
  sizes, sides, steps, UV settings.
- **Edit ▸ Move Up / Move Down** (Alt+Up / Alt+Down): reorder siblings. Engine command
  `{"op": "move", "entity", "before"}` (`scene::move_before`, undoable).
- The editor builds CSG models when publishing the scene (cached per model), draws one piece per
  brush (a click selects the brush that made the face) and see-through red/blue shapes for
  subtract/intersect brushes while editing.
- Editor API **v14**: `noor_doc_csg` (per model: brushes, triangles, volume, build time, warnings).

**Tests:** 7 editor smoke checks (create a wall, cut a doorway with an exact volume, reorder so it
cuts nothing, undo, resize and retexture, save and reopen, delete).

**Not yet:** per-face materials and UV tools (part 3), re-cutting during a drag, CSG in games
(cooking, part 4).

### 2026-11-17 — Milestone 6, part 1: the CSG kernel
**Added**
- `noor_csg` (`noor/csg/csg.h`, editor and cooking only): brushes (box, wedge, cylinder, cone,
  sphere, stairs, arch) with exact parameters, logical faces and per-face surfaces (material,
  UV scale/offset/rotation); `build_model` combines them in order (add, subtract, intersect)
  into one mesh grouped by material, with normals (smooth on curved faces), model-space box
  projection UVs, tangents, and each triangle's source (brush, face).
- **Manifold** v3.5.4 (Apache-2.0) for the booleans, pinned by commit, 3D only (no Clipper2, no
  TBB), in `third_party/manifest.json`.
- ADR-016 accepted after the spike; [docs/level/CSG.md](docs/level/CSG.md).
- `boundary.runtime_has_no_editor_code`: fails if `noor_csg` or Manifold reach `noor_player` or a
  runtime module (and checks the check can see them where they are).

**Tests:** shape volumes (exact), a doorway with a coplanar edge (volume, materials, UVs unchanged
on the wall), rotated/scaled/tilted/mirrored brushes, touching boxes, a hollow, an intersection,
100 windows (same bytes twice; ~14 ms release, 285 ms Debug).

### 2026-11-17 — Milestone 6 plan
- [M6_PLAN.md](docs/milestones/M6_PLAN.md): brushes and CSG, surface materials and UVs, cooking,
  collision and GLB export, terrain heightfields, sculpting and painting; eight parts, risks first
  (a CSG robustness spike before any UI).
- Proposed: [ADR-016](docs/decisions/ADR-016-csg-kernel.md) (CSG kernel: Manifold behind our own
  interface, decided by a spike) and [ADR-031](docs/decisions/ADR-031-terrain-heightfields.md)
  (tiled heightfields with a non-destructive layer stack).
- The ADR index now shows ADR-012, 029 and 030 as accepted.

## Milestone 5 complete (PBR lighting, HDRI and materials)

### 2026-11-17 — Milestone 5, part 8: the PBR sample and the gate review
**Added**
- **PBR-Showroom** (`samples/PBR-Showroom`): gold and white roughness sweeps, copper, chrome, red
  plastic, rubber, glass and a glowing ball, a brick wall, a concrete floor, wooden crates and a
  metal pedestal, under the sun and three HDRIs (4 scenes). CC0 textures and HDRIs from Poly Haven.
- `render.golden_Showroom_Sun/Studio/Evening/Venice`: the sample as render regression tests.
- **NoorFPS-lite** has a textured arena (brick, concrete, metal, wood; 512² CC0 sets, tiled with
  material instances).
- [M5_REVIEW.md](docs/milestones/M5_REVIEW.md): the gate passes, with documented exceptions.

**Fixed**
- Texture kind and texture-set guessing: the **last** role word in a name decides. Before,
  `wooden_rough_planks_diff_1k.jpg` was imported as a data map (too dark) and its set was called
  "wooden".
- noor_player `--frames` runs: if the last frame wasn't presented, it is drawn again (with its HUD)
  so the screenshot and report never go missing.

### 2026-11-17 — Milestone 5: texture compression and import settings
**Added**
- **Block compression** by default: colour → BC7 sRGB, data (ORM, masks) → BC7, normal maps →
  BC5 (X, Y; the shader rebuilds Z). 1 byte per pixel instead of 4; any size (edge blocks are
  padded). `"compression": "auto" | "none"` in a texture's settings (older `.noormeta` files get
  "auto"). Encoded on all cores with bc7enc + rgbcx (MIT or public domain, pinned by commit and
  SHA-256 in `cmake/NoorDependencies.cmake`, listed in `third_party/manifest.json`), cached like
  the rest of the import (importer version 2: caches rebuild once). GPUs without BC support get
  the pixels decoded back to RGBA8 on load.
- `render::TextureFormat` Bc7Srgb / Bc7Unorm / Bc5Unorm, `Renderer::supports_block_compression`
  (the `textureCompressionBC` feature is enabled when present).
- `asset::compress_texture`, `decompress_texture`, `mip_bytes`, `texture_info` (from the image
  header), `set_texture_settings` (writes the `.noormeta`, keeps the id).
- Editor: a texture's **import settings** tab (double-click a texture or **Edit**): kind, normal
  convention, compression, max size, mipmaps, and the GPU memory with and without compression.
  Editor API **v13**: `noor_doc_texture`, `noor_doc_set_texture_settings`.
- `noor_cli texture` shows the GPU format.

**Measured** (release player, a real 1254² PBR set): 8 MB → 2 MB per map, about 1 s once to
compress three maps, no visible difference (mean 0.06/255 per channel).

**Fixed:** `Renderer::set_environment` before the first frame wrote to descriptor sets that didn't
exist yet (Vulkan error, then a crash). The material preview hit it once its snapshot arrived
before the first frame; the per-frame sets are now made first.

**Tests:** BC7 colour/data quality (PSNR over 38 dB), BC5 normals within 3°, odd sizes, HDR and
"none" stay as they should, settings round trip, texture info and settings writing; 3 editor
smoke checks (the import settings tab, compression off and back on). The render goldens pass
unchanged with compressed textures.

**Not yet:** BC6H for HDR, BC4 for single-channel maps, compressed textures cooked into built games
(they compress on the first start), thumbnails.

### 2026-11-17 — Milestone 5, part 7: quality and measurement
**Added**
- **MSAA** for the scene: 4× by default (`RendererDesc::msaa_samples`, `Renderer::set_msaa`; 1/2/4/8,
  lowered to what the GPU supports). The pass draws into multisampled images and resolves into the
  HDR image. Scene pipelines and the sky are rebuilt when the sample count changes. Editor:
  Preferences ▸ Anti-aliasing (viewport option `msaa`). Player: `--msaa N`.
- **See-through materials** (`"alpha": "blend"`; drawn as cutout until now): after the opaque scene
  and the sky, sorted back to front, premultiplied (reflections keep their strength), no depth
  writes; double-sided variant. They cast shadows like cutouts.
- **Shadow culling per cascade** (`render::sphere_touches_cascade`); meshes keep a bounding sphere.
- Renderer numbers: `FrameStats` gained shadow draws, culled casters, see-through draws and MSAA
  samples; `Renderer::mesh_memory`, `render_target_memory`. noor_player's report has a `render`
  section.
- Editor: a **Render** panel (Window ▸ Render stats): GPU time per pass as bars, draws (shadow,
  culled, see-through), triangles, texture/mesh/frame-image memory, picture size and MSAA.
  Editor API **v12**: `noor_viewport_render_stats`.
- **Exposure presets** (bright sun 14, HDRI daylight 13, overcast 12, sunset 10, indoor 8, night
  4): a ▾ next to a camera's `ev100` in the Inspector and next to the material preview's
  exposure slider. New cameras get `ev100: 14`.
- [docs/rendering/MEASUREMENTS.md](docs/rendering/MEASUREMENTS.md): GPU time and memory on four
  scenes at MSAA off/4×/8×, and the decisions: BC7/BC5 compression gets its own step before big
  texture sets are used; one shared GPU device for editor viewports isn't needed yet.

**Tests:** `render.golden_Glass` (coloured panes and a clear ball over a brick box and a glowing
ball); a unit test for cascade culling; 4 editor smoke checks (Render panel numbers, MSAA 4× by
default, switching it off and back on, exposure presets). The other goldens passed unchanged
with 4× MSAA (edges move by less than the tolerance).

**Fixed:** the editor status bar said "viewport not running" while a material tab hid the scene
viewport; it now says it's resting.

**Not yet:** texture compression (next step, see above), the texture import-settings panel and
material thumbnails (moved again: they belong with compression), tinted glass (transmission),
a tonemapped MSAA resolve, cached far shadow cascades.

### 2026-11-17 — Milestone 5, part 6: the material editor
**Added**
- **Material editor** (`MaterialPanel`, `MaterialDocument`, `MaterialFile`): a `.noormat` opens as a
  tab next to the viewport (double-click it in the Content Browser, or **Edit**). It has a live
  preview (sphere, cube or plane; procedural sky or any project `.hdr`; exposure and environment
  rotation) and every material field: sRGB hex colours stored as linear numbers, sliders, and
  texture slots chosen from the project's textures. Changes save themselves to the file (no Save
  button). Ctrl+Z / Ctrl+Y undo inside the tab. Outside changes to the file are picked up.
- **Instances in the editor**: **Save as instance** (`<name>_variant.noormat`, opened at once).
  An instance shows its parents' values, marks the fields it changes (↺ gives one back to the
  parent), and `(none)` hides a parent's texture. **Open parent**.
- Editor API **v11**: `noor_doc_set_preview` (a one-object preview scene on a second snapshot
  channel) and `noor_viewport_set_preview`. Snapshots gained `show_grid` and `reload_token`; a new
  token makes the viewport re-read materials and textures at once. The preview frames its object
  when it first appears.

**Tests:** 12 editor smoke checks. The editor opens the tiles material and the preview renders.
Edits reach the file, save themselves and undo out of it. sRGB/linear round trip. Save as
instance writes the parent id. The instance inherits textures, its overrides land in
`overrides` while the parent stays untouched, and a `null` override hides a texture. Closing the
tabs gives back the scene viewport. Screenshots `editor_20`/`editor_21`.

**Not yet:** material thumbnails, several previews at once (the tab in front owns the document's
preview), exposure *presets* (a slider for now), the texture import-settings panel (moved to
part 7), the node graph (designed in ADR-030, not built).

### 2026-11-17 — Milestone 5, part 5: environments (HDRI) and image-based lighting
**Added**
- **Environments** (`noor/asset/environment.h`): an equirectangular `.hdr` or a procedural sky,
  processed on the CPU into a GGX-prefiltered cubemap (filtered importance sampling, one mip per
  roughness step) and irradiance spherical harmonics (SH9); cached in `.noor/cache`.
- **Image-based lighting** in the shader: SH9 diffuse, prefiltered specular with the split-sum
  BRDF lookup table (`render::brdf_lut`, CPU, built-in texture slot 3). It replaces part 1's
  hemisphere ambient and its analytic BRDF fit.
- The **sky pass**: the environment's background drawn behind the scene (rotation, intensity).
- `Renderer::set_environment` (`EnvironmentDesc`), `Lighting::environment_intensity` and
  `environment_rotation`; `game::EnvironmentLibrary` (player and editor, re-processes only when
  the environment changes).
- An **`environment` component** (`hdri`, `intensity` nits per unit, `rotation`, `background`);
  without one, a procedural sky from the sun's `sky_color` / `sky_luminance`. A scene with an
  HDRI and no sun entity gets no default sun (the photo has one).
- Editor: **Add to scene** on an `.hdr` makes it the scene's environment.
- Test data: three CC0 HDRIs from Poly Haven (Studio Small 09, Kloppenheim 06, Venice Sunset),
  shrunk to 256×128.

**Tests:** 4 unit tests (equirect/cube directions, constant sky = π·L irradiance and flat mips,
bright sky over dark ground, the BRDF table); `render.golden_Env_*` (white plastic and gold, rough
0 to 1, under the three HDRIs); all references updated after checking them by eye; an editor
smoke check for environments.

**Not yet:** `.exr` HDRIs, reflection probes (local reflections), a sun found from the HDRI, sky
blur control, environment preview in the editor (part 6).

### 2026-11-17 — Milestone 5, part 4: shadows and lights
**Added**
- **Sun shadows**: four cascaded shadow maps (2048², D32 array) up to `shadow_distance` (80 m),
  practical split scheme, bounding-sphere cascades snapped to whole texels (stable, no shimmering),
  3x3 hardware PCF, slope-scaled depth bias plus normal offset, fade at the end. A depth-only pass
  (`shadow.hlsl`, `vulkan_shadows.cpp`); cutout materials cast cut-out shadows.
  `render::compute_cascades` (`noor/render/shadows.h`) is pure maths and unit-tested.
- **Point and spot lights** (`render::LocalLight`, `Lighting::lights`, 16 per frame, nearest the
  camera): intensity in lumens (spots focus them into their cone), inverse-square falloff windowed
  to `range`, soft spot cones. `light` components of `type` `point` / `spot`; the sun's `shadows`
  and `shadow_distance`.
- Editor: **Create ▸ Point light / Spot light**; lights shown as markers in their colour; editor
  helpers and the ground grid don't cast shadows (`DrawItem::cast_shadow`).
- GPU timings gain a `Shadows` pass.

**Tests:** 3 cascade unit tests (ordering and sharpness, every view corner inside its cascade,
whole-texel movement when the camera moves); `render.golden_Lights` (dusk: long sun shadows, a
point light, a spot); the other golden scenes now show shadows (references updated after
checking them by eye); editor smoke checks for the light templates.

**Not yet:** shadows from point and spot lights, clustered lighting for many lights, per-cascade
culling (every caster is drawn into all four cascades), contact shadows.

### 2026-11-17 — Milestone 5, part 3: materials
**Added**
- **`.noormat` materials** (ADR-030): glTF metallic-roughness channels (base colour, metallic,
  roughness, packed metallic-roughness/ORM, single roughness and metallic maps, normal, occlusion,
  emissive), cutout alpha, double-sided, UV scale/offset. **Instances**: a parent plus overrides,
  chained up to 16 deep, loops refused, `null` removes a map. `asset::load_material`
  (`noor/asset/material.h`); the registry imports `.noormat` files.
- **Texture sets to materials**: `asset::find_texture_set` (maps found by name: albedo, normal,
  ORM/ARM, roughness, metallic, AO, emissive; previews skipped), `material_json_for`;
  `noor_cli make-material <project> <texture>`; the editor's **Make material** button.
- **GLB materials**: each glTF primitive is a submesh drawn with its material, including textures
  embedded in the GLB (`asset::Submesh`, `ImportedMaterial`, `ImportedImage`).
- Renderer: a **GPU material table** (4096, storage buffer, `create/update/destroy_material`),
  draws name a material and an index range (`DrawItem::material`, `first_index`, `index_count`),
  a double-sided pipeline, cutout discard, `update_texture` (same handle, new pixels).
  `render::Surface` (part 2's stop-gap) is gone.
- Shader: all channels; occlusion dims the ambient light; emissive relative to the exposure.
- `game::MaterialLibrary` (files, described-in-place, GLB materials), `TextureLibrary` embedded
  images; **hot reload** of `.noormat`, texture files and texture settings in the editor and in
  noor_player (checked twice a second).
- `primitive` components: `"material": "<id>"`, or material fields in place (same names).
- Editor: materials in the Content Browser, **Add to scene** on a material makes a ball wearing
  it. Editor API **v10** (`noor_doc_make_material`).

**Fixed**
- Shaders using `discard` need Vulkan 1.3's `shaderDemoteToHelperInvocation`, which wasn't
  enabled: the validation layer flagged it in the diagnostics tests. Enabled (and required).
- The golden-image tests now run with Vulkan validation and fail on any logged error (they ran
  without it and missed the above).
- Capsule UVs: the straight middle used one row of the texture (streaks). v now runs by distance
  along the surface (spheres unchanged). Found by putting a real bark texture set on a log.

**Tests:** 5 material unit tests (fields and defaults, instances, loops and missing parents,
texture sets and ORM, GLB submeshes, materials and embedded images); `render.golden_Materials`
(brick material, an instance, emissive, a texture set made by the CLI with an instance repeating
it, a GLB with an embedded texture, gold and a double-sided cutout leaf); editor smoke checks for
Make material and Add to scene. Hot reload checked by hand: a running game turned an emissive
ball from orange to blue about half a second after its `.noormat` was edited.

**Not yet:** see-through blending (drawn as cutout), the material editor (part 6), graphs, a
second UV set, glTF images in separate files. Metals look dull until environment reflections
(part 5).

### 2026-11-17 — Milestone 5, part 2: textures
**Added**
- **Texture assets** (ADR-029): .png (8/16-bit), .jpg, .tga, .bmp, .hdr under `Assets/` get a
  `.noormeta` with import settings (`kind` color/data/normal/hdr, `normal_convention`, `mips`,
  `max_size`), guessed from the file name. `asset::import_texture`, `load_texture`,
  `guess_texture_settings` (`noor/asset/texture.h`).
- Import: sRGB-to-linear for colour, DirectX green flip, halving to `max_size`, mipmaps averaged
  in linear light, renormalised normal-map mips; RGBA8 sRGB / RGBA8 / RGBA16F. Cached in
  `<project>/.noor/cache/textures` (content hash + settings + importer version).
- Renderer: `create_texture` / `destroy_texture` / `texture_memory`, a **bindless texture table**
  (4096 slots, descriptor indexing; built-in white, flat normal and a missing-texture checker),
  one trilinear 8x anisotropic sampler. `DrawItem::surface` (`render::Surface`: base colour and
  normal textures, roughness, metallic, normal strength, UV scale/offset). The camera matrix
  moved into the frame uniforms; push constants carry the surface.
- Shader: base colour texture, tangent-space **normal maps** (OpenGL convention), roughness and
  metallic per draw.
- `primitive` components: `base_color_map`, `normal_map`, `normal_scale`, `roughness`,
  `metallic`, `uv_scale`. `game::TextureLibrary` loads them for noor_player and the editor.
- Editor: textures in the Content Browser; **Add to scene** on a texture makes a cube wearing it.
- `noor_cli texture <files>`: how the engine reads each image (kind, size, mips, GPU memory).
- `.noor/` (project derived data) is ignored by Git everywhere.

**Fixed (found by the editor smoke test before release):** closing a viewport that had shown a
texture crashed the editor (textures were released after their renderer).

**Tests:** 7 texture unit tests (name guessing, linear-light mips, data vs colour, normal map
renormalisation and DirectX flip, HDR to 16-bit float, broken files, cache and registry);
`render.golden_Textured` (checker floor with mips, a brick normal map on a wall and a ball, a
metal ball); an editor smoke check for texture assets. Checked on a real PBR library: every
albedo, normal, roughness, AO, height, depth, curvature, cavity and ORM map got the right kind.

**Not yet:** compression (part 7), GLB textures (part 3, with materials), .exr (part 5), hot
reload of changed textures, an import-settings panel in the editor.

### 2026-11-17 — Milestone 5, part 1: the frame foundation
**Added**
- The scene is lit into a **16-bit float HDR image** in physical units, then **exposed and
  tonemapped** to the screen (ADR-012). Editor gizmos and the HUD are drawn after tonemapping in
  their own display colours. `vulkan_frame.cpp`, `tonemap.hlsl`.
- **PBR shading core**: GGX / height-correlated Smith / Schlick specular, Lambert diffuse; ambient
  from a ground-to-sky hemisphere with a specular part. Surfaces are rough dielectrics until
  materials (part 3). Documented in `docs/rendering/PBR.md`.
- `render::Lighting`: sun direction, colour and illuminance (lux), sky colour and luminance
  (nits), exposure (EV100, default 14), tonemapper (Khronos PBR Neutral default, ACES fit, none),
  sky or fixed background. `FrameDesc::lighting`.
- A **`light` component** (`"type": "directional"`, `color`, `illuminance`, and for now
  `sky_color`, `sky_luminance`): the sun shines along its entity's forward. `camera` gains
  `ev100` and `tonemapper`. **Create ▸ Sun (directional light)** in the editor.
- Vertices gain **UVs and tangents** (`render::Vertex`, `asset::MeshVertex`, 60 bytes): GLB
  `TEXCOORD_0` and `TANGENT`, tangents computed from UVs when missing; the built-in box, sphere and
  capsule have UVs.
- **GPU time per pass** (`FrameStats::passes`: Scene, Tonemap, Gizmos, HUD); in noor_player's
  `--report` as `gpu_passes`.
- **Golden-image render tests**: `noor_image_compare` (shrinks a screenshot, compares it with a
  PNG reference with a tolerance, writes a diff picture) and `tests/data/render_scenes`
  (Daylight, Sunset). `NOOR_UPDATE_GOLDEN=1` refreshes references. stb_image and stb_image_write
  (public domain/MIT, pinned) for reading and writing images.

**Changed**
- Scenes look different: lit by a real sun and sky instead of a fixed fake light. Very dark
  colours now look very dark in sunshine (that's physically right; see PBR.md).
- The player test checks that the centre pixel is the red box by colour, not brightness.

**Tests:** 3 new render tests (two golden images and one that must fail on a changed picture).
noor_diag's exact-purple background check still passes through the new tonemap pass.

**Not yet:** materials and textures (parts 2–3), shadows (4), HDRI skies (5), MSAA and the
passes inspector in the editor (7), AgX (licence check), depth pre-pass (when measured).

### 2026-11-17 — Milestone 4, part 8: NoorFPS-lite and the Milestone 4 review
**Added**
- **NoorFPS-lite** (`samples/NoorFPS-lite`): a first-person target shooter made only with the
  public script API. Walk, sprint, jump, look, shoot gold targets against a one-minute clock,
  footsteps, shot/hit/round-end sounds (synthesised for the sample), physics crates to knock about,
  a HUD (score, accuracy, clock, hit marker), pause on Esc, a self-playing demo before the first
  click. Written in C# (`Scenes/Main.noorscene`) and as a Lua twin (`Scenes/Main_Lua.noorscene`).
- `noor_player --no-input`: the game gets no keyboard or mouse (tests).
- The player's `--report` includes the last frame's HUD text (`hud_text`), so tests can read a
  game's score.
- **Milestone 4 review** (`docs/milestones/M4_REVIEW.md`): gate passed, exceptions listed.

**Changed (breaking for C# scripts)**
- C# `Entity` is now a class (a handle, equal by id) instead of a struct: `Entity.Position = v`
  and `hit.Entity.Position = v` used to fail to compile (CS1612). `Noor.Find`, `Parent` and
  `Child` still return `Entity?`; drop any `.Value` on them.

**Fixed**
- `raycast(…, ignore)` didn't skip a character's controller body, so a shot from the player's eyes
  hit the player at distance 0.
- A real click on a built game's window could change an automated test's result: the player and
  sample tests now run with `--no-input`.

**Tests:** `cli.build_sample_fps` builds the sample with noor_cli; `sample.fps_csharp` and
`sample.fps_lua` run its demo for 15 s with no input and read the score off the HUD (≥ 10; 24 in
practice). A unit test for raycasts ignoring a character.

**Not yet:** see the review's exceptions: prefab spawning, timers, `require`, gamepad, app-local
runtimes, NoorCode completion and debugging, rays vs triggers.

### 2026-11-16 — Milestone 4, part 7b: NoorCode MVP
**Added**
- **NoorCode**, the built-in script editor: scripts open as tabs next to the viewport (Inspector
  ▸ Open, or the links in the Console). Built on **AvaloniaEdit** 12.0.0 (MIT, NuGet; editor only).
  Lua/Luau and C# highlighting (our own `.xshd` files in the NoorTheme colours), line numbers,
  find (Ctrl+F), undo, spaces for tabs.
- Ctrl+S saves the script whose tab has the keyboard; a running game reloads it as before. Play and
  Build Game save open scripts first. Closing a tab, the project or the editor asks about unsaved
  scripts. A file changed outside reloads in its tab; with unsaved edits too, a bar asks which to keep.
- Errors that name a script line (Lua errors, C# compile errors, C# exceptions) are marked on the
  line in red and listed under the editor (click: go to the line). In the Console they get a
  `File:line ↗` link that opens the script there (`ScriptLocation`).
- Preferences ▸ Scripts: "Open scripts in NoorCode" (on by default); the external editor stays as
  NoorCode's "Open in …" button, and is used directly when NoorCode is off.

**Fixed**
- C# compiler messages in the PC's language (Polish here) were garbled ("Oczekiwano Ļ›rednika"):
  the editor and noor_cli now read `dotnet build` output as UTF-8.

**Tests:** 15 new editor smoke checks (133 total): highlighting loads, error messages resolve to
script lines, open at a line, typed keys go to the text and not the editor's shortcuts, Ctrl+S,
Play saving first, a compile error marked on line 7 and linked from the Console, outside changes,
closing. Looked at in a screenshot.

**Not yet:** code completion, error checking while typing, a debugger (spec 7 is the full IDE:
these need language services, Roslyn for C# and a Luau analyser); open tabs aren't remembered
between sessions.

### 2026-11-16 — Milestone 4, part 7a: the HUD
**Added**
- Scripts draw a HUD every frame: Lua `noor.ui.text(text, x, y [, {size, color, anchor}])`,
  `noor.ui.rect(x, y, w, h [, {color, anchor}])`; C# `Ui.Text(...)`, `Ui.Rect(...)`.
  `noor_rt_api` **v3**. UI units (the screen is 720 tall), nine anchors, `#RRGGBB[AA]` colors;
  wrong colors/anchors are script errors naming the problem.
- `runtime::UiCommand` / `World::ui()` (cleared each frame), `api::parse_color` / `parse_anchor`.
- Renderer: a 2D **overlay** pass over the 3D scene (`OverlayVertex`, `set_overlay_atlas`;
  `vulkan_overlay.cpp`, `overlay.hlsl`): alpha blending, solid shapes, signed-distance-field text
  (four samples per pixel, so small text keeps thin strokes).
- `game::Hud`: bakes the built-in font's distance fields with **stb_truetype** (public domain/MIT,
  pinned) at start (~1024×512 atlas), lays out text (UTF-8, line breaks, kerning, anchors) and
  rectangles. Font: **Atkinson Hyperlegible** (SIL OFL 1.1, pinned, embedded in the engine);
  Latin, Latin-1, Latin Extended-A (Polish…) and common symbols. Builds carry its licence in
  `licenses/`.
- The HUD shows in noor_player and in the editor's Play. The first-person player template draws a
  crosshair while looking, and a "Click to look around" hint until then.

**Tests:** 5 HUD unit tests (colors, anchors, placement maths, measuring, font coverage incl.
Polish, vertices per glyph, Lua `noor.ui` incl. bad options); the player tests check the HUD's
vertex count (48; 60 with a C# label) and that the pixel at UI (40, 40) is the script's green square.
Small text was checked by eye in screenshots (14 units: fixed thin strokes by supersampling).

**Not yet:** images/icons on the HUD, other fonts, clickable buttons, text wrapping.

### 2026-11-16 — Milestone 4, part 6b: C# scripts in built games
**Added**
- `scripting::start_dotnet`: noor_player starts .NET with hostfxr (the .NET runtime installed on
  the PC; no SDK) and loads the game's compiled scripts through `ScriptHost.Boot`. ADR-008 notes.
- `noor_cli build` compiles `Scripts/**/*.cs` (Release, with line numbers) into `managed/` with
  NoorEngine.Scripting, before writing anything; compile errors read `Scripts/X.cs:7: CS1002: ...`.
- The engine build now compiles NoorEngine.Scripting (`noor_scripting_managed`, needs the .NET SDK;
  without it CMake says games can't have C#). CI installs .NET before configuring.
- NoorEditor's File ▸ Build Game packs C# scripts too.
- NoorEditor: **Edit ▸ Preferences ▸ Scripts ▸ Code editor** (asked for by Tom): Windows default, an
  editor found on the PC (VS Code opens the Scripts folder at the line, Visual Studio, Rider,
  Notepad++, Notepad), or any program with `{file}` / `{line}` / `{folder}` arguments. The
  Inspector's Open button uses it. The dialog's layout hasn't been looked at in a screenshot yet.

**Fixed**
- **Play at the screen's rate** (Tom: walking in Play "doesn't feel like 60 FPS", flying is fine):
  the editor ticked the game from a 16 ms UI timer, which Windows rounds to ~15.6 ms steps and the
  busy UI thread delays: **34 updates/s** measured (each costing ~2 ms). Now the viewport's render
  thread runs one game frame before every frame it draws, like the fly camera
  (`noor_viewport_set_play`, editor API **v9**; the document locks its running world for the UI
  thread's panel reads; a script's mouse-capture request reaches the UI thread as a viewport event).
  Measured: 58–60 updates/s; the editor smoke test now checks it (118 checks).
  Regression found by Tom right after: the player couldn't move. The viewport's "has focus" used
  `GetFocus()`, which only answers for the calling thread's windows, so on the render thread the
  game got no keys (the smoke test's test-pointer mode counts as focused, so it passed). Focus is
  now tracked from WM_SETFOCUS/WM_KILLFOCUS; a unit test reads it from another thread.
- **Smooth motion** (Tom: "not as smooth as in other games"): physics steps at a fixed 60 Hz, and
  even on a 60 Hz screen frame times wobble, so some frames got no step and some two: walking
  stuttered slightly. Physics now tells the World where to *draw* what it moves, blended between its
  last two steps (`World::set_render_pose`, `fixed_alpha`); the editor's Play and the player draw
  that, children (the player's camera) follow, scripts and the simulation keep the real transforms.
  Costs one physics step (1/60 s) of visual lag, as in other engines. Test: with frame times wobbling
  ±1.5 ms the simulated position stalls on some frames while the drawn one moves within 12 mm of
  speed × dt every frame. Not yet checked by hand.
- Built games: the view jerked on the click that captures the mouse (entering relative mode warps
  the hidden cursor to the window centre, which arrived as movement); switching to another window
  now releases the mouse like Esc. Found by Tom; not yet re-checked by hand.
- The copies of noor_player next to noor_cli (from 6a) only refreshed when noor_cli itself relinked;
  now a separate step refreshes them every build.
- Scripts loaded by a host that isn't the editor couldn't find NoorEngine.Scripting (hostfxr loads it
  in its own context); the scripts' context now resolves it to the host's copy.

**Tests:** `cli.build_csharp`, `cli.build_csharp_error` (CI) and `player.built_csharp_game` (GPU):
a box lifted by a C# script in a built game rises exactly 2 m in 120 frames. Editor smoke: the
built game includes the compiled C# (114 checks). The game the editor smoke test builds (player,
physics, Lua, C#, sound) also ran 180 frames without errors when started by hand.

**Not yet:** self-contained builds (bundling .NET); `noor_player <project>` doesn't compile C#.

### 2026-11-16 — Milestone 4, part 6a: games that run without the editor
**Added**
- `game` module (ADR-028): `make_world()` (the world with every engine system), `collect_view()`
  (models, primitive shapes, the game camera from a running scene), `SceneRenderer`, primitive
  meshes. The editor's Play and its render snapshots now go through it, so editor and player run
  the same code.
- `noor_player`: runs a game in its own window (`noor_player` next to a built game, or
  `noor_player <project folder>`). Keyboard and mouse into the game's input actions, mouse capture
  on the script's request (Esc releases), sound, physics, Lua. Test options `--frames`, `--report`,
  `--screenshot`, `--no-audio`.
- `noor_cli build <project> <out> [--scene]`: validates, then writes `<Name>.exe` (the player),
  `game.json` and `game/` (the project's files without derived data). Rebuilds replace an earlier
  build; non-empty folders and folders inside the project are refused.
- NoorEditor: **File ▸ Build Game…** (Ctrl+B): saves if you like, asks for a folder, builds starting
  with the open scene, then offers **Play it** / **Show folder**. Output goes to the Console.
- `asset::Project::default_start_scene()`.

**Tests:** `cli.build` (headless, CI); `player.project` and `player.built_game` (GPU, local): 120
fixed frames of `tests/data/player_project` from a project and from a build; the ball must rest on
the floor, a Lua script must have lifted a box exactly 2 m, and the centre pixel must be the red box
seen through the scene's camera. Editor smoke test: File ▸ Build Game (113 checks).

**Not yet:** C# scripts in builds (done in 6b, above). Builds need the Visual C++
2015–2022 runtime on the PC; Debug tools make games that only run where Visual Studio is installed
(noor_cli warns). No packing/cooking: a build's content is plain project files, and the registry
is rescanned (assets hashed) at every start. No game icon, no fullscreen option yet.

### 2026-11-15 — Milestone 4, part 5: sound
**Added**
- `audio` module (ADR-011): **miniaudio** (pinned single header, MIT-0/public domain) as
  `audio::AudioSystem`. `audio_source` (clip, volume, pitch, loop, spatial, min/max distance, bus,
  play on start, stream), `audio_listener` (else the first camera hears), buses master/music/sfx/
  ui/voice, inverse-distance 3D attenuation, one-shots that clean up after themselves.
- `.wav`, `.flac`, `.mp3` files are `audio` assets (registry importer "audio").
- Without a sound device (CI, tests, `NOOR_AUDIO_DEVICE=none`) the mixer still runs, into memory,
  so the same code paths are tested everywhere.
- Scripts: Lua `e:play_sound()`, `e:stop_sound()`, `e:is_playing()`, `noor.play_sound(clip [,
  position [, volume [, bus]]])`, `noor.set_volume(bus, v)`; C# `Entity.PlaySound()/StopSound()/
  IsPlaying`, `Noor.PlaySound(...)`, `Noor.SetVolume(...)`. `noor_rt_api` **v2**.
- `System::starts_late()`: script systems start entities after engine systems, so `on_start` can
  use the entity's sound (and anything else engine systems set up).
- Editor API **v8**: `noor_doc_set_audio_device`. NoorEditor: audio assets show as **Sound** (♪);
  **Add to scene** on one makes an emitter; **Add component ▸ Sound**; the first-person player's
  camera has an `audio_listener`.

**Tests:** 4 audio tests (silence when idle; play/loop/stop; near is louder than far and a muted
bus is silent, measured from the mixed samples; one-shots clean up, a missing clip is an error,
not a crash; a Lua script driving it all). The editor smoke test writes a WAV, adds it to the
scene, and plays it with a C# script (looping emitter + one-shot), silently.

**Fixed**
- Create ▸ … and Add to scene did nothing while playing (only a Console warning); they now ask to
  stop the game first.
- Sound stopped for good after Bluetooth earbuds went into their case and came back (found by Tom).
  miniaudio follows default-device changes but ignores failures, and Bluetooth reconnects in stages.
  NoorEngine now owns the device: one that stops pulling audio for 1 s is rebuilt, with retries
  every 1-5 s while there's none (sounds keep playing silently meanwhile). Also: a game started
  with no device picks one up when it appears. Tested by stopping the device as Windows would
  (local only: CI has no sound card, so that test skips there), and by Tom with real Bluetooth
  earbuds: into the case and out again during Play, the music came back.
- **Tests that never ran.** CMake splits lists on `;`, so 15 test cases with a `;` in their name
  became ctest entries that matched nothing and "passed". Renamed; every ctest entry that runs zero
  test cases now fails. Of the 15, one had a wrong expectation (a stopped sound fades out for a few
  ms instead of cutting off with a click) and was corrected; the rest passed as they were. 138 real
  tests now, instead of 152 entries.

**Not yet:** pause, fades, voice limits, device switching, `.ogg`, occlusion/reverb. Automated tests run
without a device; speaker output was checked by ear by Tom (music box emitter, walking around it).

### 2026-11-15 — Milestone 4, part 4: C# scripts
**Added**
- `noor_rt_api.h`: the scripting ABI as two tables of C function pointers, `noor_rt_api` (engine
  functions scripts call; the Lua set) and `noor_rt_host` (the managed host). ADR-008 updated.
- `scripting::CSharpScriptSystem`: the engine half; same hook order and error handling as Lua;
  reports once (not every frame) when no host is available.
- `engine/scripting/dotnet/NoorEngine.Scripting`: `NoorScript`, `Entity`, `Noor`, `Input`,
  `RaycastHit`, and `ScriptHost` (loads compiled scripts from memory into a collectible
  `AssemblyLoadContext`, unloads on Stop; exceptions never cross into native code and are reported
  with type, message and file:line).
- Editor API **v7**: `noor_rt_get_api`, `noor_doc_set_script_host`. NoorEditor: Play compiles C#
  scripts (`Scripts/Scripts.csproj` generated for IDEs; `dotnet build` ~1.2 s), compile errors go
  to the Console as `Scripts/X.cs:7: CS1002: ...` and Play doesn't start; **Add component ▸ C#
  script…** with a template.
- `docs/scripting/CSHARP_API.md`.

**Tests:** 3 engine-half tests with a fake host (hook order, callbacks into the world, input,
exceptions, missing class, missing host, id round trip); the editor smoke test writes a real C#
script, plays it (87° in ~1 s at 90°/s, `Entity.Set`, `Noor.Find`, `Noor.Log`), checks a compile
error (file:line, no play), an exception (line, game continues) and reload after editing.
Provoked errors are counted so the "zero errors" budget stays meaningful.

### 2026-11-15 — Milestone 4, part 3b: a walking first-person player
**Added**
- `character` component on Jolt `CharacterVirtual`: capsule with feet at the entity origin, slides
  along walls, walks up steps (`step_height`) and slopes (`max_slope`), sticks to the floor going
  down, jumps only from the ground; an inner body so raycasts and triggers see the player.
  `character_move` (per frame), `character_jump`, `is_grounded` in C++ and Lua.
- `camera` component: while playing, the viewport looks through the first camera entity
  (picking, gizmos and `world_to_screen` use the same camera).
- Mouse capture for look: scripts ask (`noor.input.capture_mouse`), the host captures only a
  focused game view (cursor hidden and clipped, raw mouse movement as `look_x`/`look_y`); Escape,
  focus loss and Stop always release it. Tests never take the real mouse (test pointer mode).
  Editor API: `noor_viewport_set_mouse_capture`, `noor_viewport_mouse_captured`.
- `e:child(name)` in the script API.
- NoorEditor: **Create ▸ Player (first person)** (player with character + script, a capsule body and
  an eye-height camera; one undo step) and the `Scripts/PlayerController.luau` template (WASD, Shift
  sprint, Space jump, mouse look after a click, Esc to release).

**Tests:** 5 character tests (standing/walking/stopping, jump only from the ground, 0.3 m kerb up and
2 m wall stop, raycast and trigger see the player, a Lua script steering and asking for the mouse);
the editor smoke test creates the player, plays, checks the view is the player's camera, walks
(3.1 m), jumps (1.23 m peak; 1.27 predicted), captures and releases the mouse (91 checks).

**Fixed (tests)**
- Under ctest the editor is launched from a background process and Windows may not let it take
  the foreground, so keyboard focus (and held keys) came and went. The viewport's test mode now
  counts as focused, and the editor asks the engine (`noor_viewport_has_focus`) instead of Windows
  whether the viewport has focus. 139/139 Debug, 133/133 Release without GPU.

### 2026-11-15 — Milestone 4, part 3a: physics (Jolt)
**Added**
- Jolt Physics 5.6.0 (MIT) pinned, built for a portable SSE4.2 baseline (no AVX2), without its GPU
  compute, debug renderer or profiler.
- `engine/physics`: `PhysicsSystem`, a runtime system. `collider` (box, sphere, capsule, mesh;
  offset; trigger) and `rigidbody` (dynamic, kinematic, static; mass, friction, restitution,
  damping, gravity scale) components; sizes follow the entity's world scale; dynamic bodies move
  their entities (also as children of moved parents), kinematic bodies follow them; fixed 60 Hz
  step after the scripts' fixed update; raycasts; velocity, impulses, forces; collision and trigger
  enter/exit events. Mesh colliders use the model's own triangles, or a referenced collision mesh
  (Noor Interchange collision nodes).
- Sleeping bodies don't fake "exit" events: Jolt drops a resting body's contacts when it sleeps;
  those pairs are parked and only reported if the body wakes up and really leaves.
- `runtime::World` events (cleared each frame); `api::world_matrix`, `world_rotation`,
  `set_world_pose`.
- Lua: `noor.raycast`, `velocity`/`set_velocity`/`add_impulse`/`add_force`, hooks
  `on_collision_enter/exit`, `on_trigger_enter/exit`.
- Primitive shapes (`primitive` component: box, sphere, capsule, plane, color) rendered and pickable
  in the viewport. Editor API **v6**: `create` command (one undo step). NoorEditor: **Create** menu
  (empty, box, sphere, capsule, floor, physics crate, physics ball), **Add component ▸ Physics /
  Shape** presets.

**Fixed**
- Keys typed into the viewport could reach whichever editor control last had focus: Space "clicked"
  a focused button (found when a play-mode test's Space press sometimes did nothing). Viewport keys
  are now never delivered to editor controls.

**Tests:** 8 physics tests (falling and resting, one enter and no fake exit when asleep, wake-up
exit/enter, raycasts, bounce, impulse, trigger, kinematic pushing, mesh collider, refused dynamic
mesh, parented bodies, Lua hooks/raycast/impulse); the editor smoke test builds a floor and crate
from the Create menu and drops a ball on the BadBuilding roof through a GLB mesh collider (81 checks).
Its play-mode input checks now test direction, not speed: under load the editor's play timer
advances less game time (speed itself is unit-tested). 134/134 Debug, 128/128 Release without GPU.

### 2026-11-15 — Milestone 4, part 2: Lua scripts (Luau)
**Decided:** ADR-009 accepted: Luau (project owner confirmed). Pinned Luau 0.745 (MIT).

**Added**
- `runtime::api`: the scripting API in one place (entities, local/world transforms, Euler in the
  editor's convention, directions, component data, input actions/axes, time), for Lua now and C#
  in part 4.
- `engine/scripting`: `LuaScriptSystem`, a runtime system. `script` component
  (`{"file": "Scripts/X.luau", "params": {...}}`); hooks `on_start`, `on_update`,
  `on_fixed_update`, `on_late_update`, `on_destroy`; one instance (`self`) per entity with
  `self.entity` and `self.params`; Luau vectors for positions/directions; `noor.log` with file and
  line. Sandbox: Luau's safe libraries, read-only globals and built-ins, per-file globals; a hook
  that runs over 0.25 s is stopped; an erroring script is reported (file, line, stack) and
  switched off for that entity while the game goes on.
- NoorEditor: **Add component ▸ Lua script…** (creates `Scripts/<Name>.luau` from a template),
  the script card shows the file with **Open** and its params as editable fields (**+ Param**).
- `docs/scripting/LUA_API.md`.
- Tests: 8 Lua tests (hooks/params/vectors, input/find/rotation/directions, runtime and syntax
  errors with file:line, sandbox, endless-loop budget, destroy) and an Euler round trip; the editor
  smoke test creates a script from the Inspector, edits it and drives the building with D and
  Space in play mode (74 checks).

### 2026-11-15 — Milestone 4, part 1: the runtime module and game input
**Decided:** ADR-027 (runtime, frame order, one scripting API for C# and Lua), ADR-008 (.NET 10
CoreCLR hosting), ADR-010 (Jolt physics), ADR-011 (miniaudio), ADR-009 proposed (Luau recommended;
one question open). Plan: `docs/milestones/M4_PLAN.md` (8 parts, risks).

**Added**
- `engine/runtime`: `World` (systems; frame order start → update → fixed 60 Hz steps (max 5) →
  late update → destroy; dt capped at 0.1 s; spawn/destroy with whole subtrees), `Input` with
  actions and axes (`Settings/input.json`, defaults: move_x/move_y, jump, sprint, crouch, fire,
  aim, interact, pause; look_x/look_y from the mouse), built-in behaviours moved here
  (`rotator`, new `input_mover`).
- Editor API **v5**: play mode runs a `World`; `noor_doc_tick` takes the game-view viewport, whose
  keyboard and mouse are the game's input while it has focus (not while the right button flies
  the editor camera). While playing, keys typed into the viewport go to the game; only Ctrl
  shortcuts reach the editor.
- `noor_viewport_test_pointer`: tests inject the pointer instead of posting mouse messages.

**Fixed**
- The editor smoke test's clicks and drags depended on where the real cursor was: Windows sends a
  mouse-move with the real cursor position after `SetCapture`, so a cursor resting over the editor
  bent every drag. Found when the test failed identically twice in a row.

**Tests:** 7 runtime tests (frame order, fixed step, lifecycle, input edges/axes, input.json,
behaviours); the editor smoke test plays with keys pressed in the viewport (66 checks).
115/115 Debug, 109/109 Release without GPU.

## Milestone 3 complete

### 2026-11-15 — Milestone 3 complete: play mode, live reload, gate review
**Added**
- ADR-026 (play mode). Editor API **v4**: `noor_doc_play` / `stop` / `tick` (a runtime copy of
  the expanded scene; edits, undo and scene switches refused while playing; Stop restores) and
  `noor_doc_poll_files` (watched asset files: changed models get a new version in the render
  snapshot and viewports reload them; changed prefabs are re-read).
- `scene::update_behaviours`: the first built-in behaviour, `rotator` (axis, degrees per second).
- NoorEditor: **▶ Play / ■ Stop** (Ctrl+P), running values shown in the panels a few times per
  second, gizmos hidden and Inspector read-only while playing; asset files polled twice a second;
  the hierarchy only rebuilds when its structure changes.
- `docs/milestones/M3_REVIEW.md`: gate review (complete with 6 exceptions). HiDPI: checked by
  hand at 150 % (the editor followed a live scaling change and looked right); the automated test
  has run at 100 % only.
- Tests: rotator unit test; document tests for play/stop, live prefab reload, undo edge cases
  (after save, redo branch, transactions, scene switch); the editor smoke test plays, live-reloads
  a model, edits through a floating Inspector and asserts swapchain = control × render scaling
  (61 checks). 108/108 Debug, 102/102 Release without GPU.

### 2026-11-15 — Milestone 3, part 3: scale gizmo, snapping, preferences, component editing
**Added**
- Editor API **v3**: `NOOR_GIZMO_SCALE` (handles along the entity's own axes + a uniform centre
  cube) and `noor_viewport_set_options` (snap on/off and steps, fly speed, look sensitivity,
  gizmo size). Snapping: moves land on a world grid, rotations on angle steps, scales on scale
  steps; holding Ctrl while dragging inverts snapping. Each drag's result is computed once per
  frame and used both for the preview and the final command, so what you see is what you get.
- NoorEditor: **R** = scale tool; **Snap** toggle in the viewport toolbar; **Edit ▸ Preferences**
  (camera and gizmo settings, saved per user); the **Inspector edits components**: numbers, text,
  true/false and short number lists, add/remove fields, add (marker or custom) and remove
  components, each change one undo step. Engine-managed components (model, mesh, prefab) stay
  read-only.
- Tests: snap and scale-handle unit tests (one caught a divide-by-zero in the centre-cube pick);
  the editor smoke test drags the scale handle (2.01x), makes a snapped move (lands on x = 15),
  saves preferences and edits/removes/undoes a component. 101/101 Debug.

### 2026-11-15 — Fly mode
Requested after the first hands-on try of the gizmos on the street demo ("no fly mode?").
- Viewport camera: **hold the right button** to look around and fly (WASD, Q/E down/up, Shift x4,
  wheel changes the speed while flying). Orbit moved to **Alt + left-drag**; middle-drag pans and
  the wheel zooms as before. Fly keys are not forwarded as editor shortcuts while flying.
- `editor::EditorCamera` (orbit/look/pan/zoom/fly) moved out of the render loop into the
  unit-tested viewport tools; the native child window tracks fly keys and Alt.
- Tests: 2 camera unit tests; the editor smoke test flies (right button + W through the viewport's
  own window: 4.8 m in 0.6 s) and mouse-looks (40 checks). 99/99 Debug.

### 2026-11-15 — Milestone 3, part 2: click to select, gizmos, layout memory
**Added**
- Editor API **v2** (ADR-025 addendum): `noor_viewport_set_selection` (highlight),
  `noor_viewport_set_gizmo` (move/rotate gizmo on one entity), `noor_viewport_poll_events`
  (`click`, `transform`), `noor_viewport_world_to_screen`. `set_transform` accepts an undo `label`.
- Viewport: left click picks the nearest mesh or marker (ray vs. triangles, AABB first); selected
  objects drawn with a purple tint; **move gizmo** (X/Y/Z arrows + XZ plane square) and **rotate
  gizmo** (X/Y/Z rings, around the entity's origin), world-aligned, constant size on screen, hover
  highlight; drags previewed live and reported on release.
- Renderer: per-draw `tint` and `overlay` draws (drawn after the scene with depth cleared). Push
  constants stay at 128 bytes (the world 3x3 is sent as three columns).
- Math: `inverse(Mat4)`, `transform_vector`, `conjugate`/`normalize_quat`/`rotate` for quaternions.
- Render snapshots carry every expanded entity (world matrix, world rotation, local transform,
  editable flag) and marker ids.
- NoorEditor: Select/Move/Rotate tools (Q/W/E, toolbar, Edit menu); a click selects the whole
  object first, clicking again inside the selection goes one level deeper; gizmo results become
  one undo step each ("Move"/"Rotate"). Panel sizes and active tabs are remembered;
  Window ▸ Reset Layout.
- Tests: 7 viewport-maths tests (rays, triangles, boxes, handle picking, drags, rotation sign,
  projection round trip), 2 math tests; the editor smoke test now clicks and drags in the real
  viewport (posted mouse messages to its own child window): pick, drill down, empty click, a 3 m
  X drag, a 40° yaw drag, undo, layout save/restore/reset (37 checks). 96/96 Debug, 90/90 Release
  without GPU.

**Not yet:** snapping, local-space gizmos, scale gizmo, multi-select, box select, an outline
instead of a tint; panels moved to another dock or floated are not remembered.

### 2026-11-15 — Milestone 3, part 1: NoorEditor exists
**Added**
- ADR-025: the engine owns the document (project, registry, scene, undo history, prefabs); the
  editor reads JSON snapshots and changes things only with commands. Editor API **v1**:
  `noor_doc_*` (open/create project, assets, scenes, open/new/save scene, scene snapshot,
  `execute` with `set_transform`/`rename`/`set_component`/`remove`/`reparent`/`add_asset`/
  `begin`/`end`, undo, redo, revision) and `noor_viewport_set_document` / `noor_viewport_frame`.
- Viewports render the document's scene (prefabs expanded, models cached by path) from immutable
  snapshots published by the document; orbit camera (right-drag orbit, middle-drag pan, wheel
  zoom, framing). Native child window: middle button, wheel, capture loss; keys are forwarded to
  the host window so editor shortcuts work while the viewport has focus.
- `editor/NoorEditor`: project hub (recent projects, new, open), docked Viewport / Scene /
  Inspector / Content Browser / Console, menus, status bar, Ctrl+S/Z/Y, F, Delete, unsaved-changes
  prompts, window size remembered. Inspector edits are one undo step each; rotation shown as Euler
  degrees (yaw about +Y); comma decimals accepted.
- Tests: `noor_editor_api_tests` (document API through the DLL, no GPU, runs in CI) and
  `editor.smoke` (builds the editor and runs `--smoke`: 27 checks incl. inspector edits,
  undo/redo, save file contents, delete/undo, floating the viewport, zero validation/engine
  errors). 85/85 locally. CI now builds NoorEditor.

**Fixed**
- Prefab expansion: a non-string entry in an instance's `removed` list was read through a
  default-constructed `Result<Guid>` (which counts as success) and skipped the nil id. Now ignored.

**Not yet:** gizmos, viewport picking, selection highlight, component editors, play mode, layout
persistence (window size only), several viewports on one document sharing a GPU device.

## Milestone 2 complete

### 2026-11-15 — Milestone 2 complete: prefabs, scene rendering, review
**Added**
- ADR-024 (prefabs). `Guid::derive` (RFC 9562 v8, SHA-256) for stable expanded ids.
- `scene::Prefab` (`.noorprefab`, format 1), `expand_prefabs` (overrides: name/layer/tags/
  transform parts/components; `removed` subtrees; nested prefabs and variants; cycle and depth
  detection; unused overrides kept and reported), `make_prefab` (instance keeps the old root's id),
  `make_instance`, `PrefabLibrary` (loads prefabs through the registry).
- Registry: `.noorprefab` files are assets (importer `prefab`, GUID = the prefab's own id).
- `noor_cli make-prefab`, `add-prefab`; `validate` checks the expanded scene.
- `noor_diag --project <dir> --scene <file>`: renders a scene with prefabs expanded; hot-reloads
  when the scene, a prefab or a model changes.
- `docs/milestones/M2_REVIEW.md`: gate review (complete, 5 documented exceptions).
- Tests: 10 prefab tests (incl. exact world-matrix equivalence after making a prefab), CLI workflow
  makes and places prefabs. 80/80 (Debug), 75/75 non-GPU (Release).

**Measured:** street demo, 12 instances of 4 BadBuilding prefabs → 412 entities, 334 draws,
56,306 triangles; Debug + validation CPU 1.7 ms, GPU 0.02 ms.

### 2026-11-15 — Log file follows live
- `FileLogSink` flushes routine lines at least once per second (warnings and errors still at once).
  Found during Tom's hot-reload demo: the log only showed the reloads after the window closed.
- Hot reload demonstrated on Tom's machine: five BadBuilding buildings (panel block, kamienica,
  courtyard block with gate passage, long PRL slab, house) swapped live into a running noor_diag.

### 2026-11-15 — Milestone 2, second increment: undo/redo, format migrations, hot reload
**Added**
- `scene::History` + commands (`set_transform`, `rename`, `set_component`, `add_entity`,
  `remove_entity` with subtree, `reparent` with cycle check): transactions, labels, save tracking
  (`modified()` is false again after undoing back to the saved state), step limit.
- `asset::Migrations`: versioned document upgrades (N → N+1 steps, newer versions refused).
- **Scene format 2**: entities gain `layer` and `tags` (spec 9.1). Format 1 files upgrade on load;
  saving keeps the original as `.bak`.
- `asset::FileWatcher`: polling watcher that reports a change once the file has settled.
- `noor_diag --glb` hot-reloads the model when the file changes (keeps the old model if the new
  file is broken).
- `noor_cli migrate <project> [--dry-run]`; `validate` notes scenes in an older format.
- Tests: 10 new unit tests incl. 25 random 40-edit sequences that must undo to the byte-identical
  original and redo to the byte-identical result; CLI workflow covers migration. 69/69 pass (Debug),
  64/64 non-GPU (Release). Hot reload verified end to end by overwriting a GLB while noor_diag ran.

**Fixed**
- File watcher counted one extra poll before reporting a settled change (found by its test).

**Known limitations**
- No prefabs yet; editor UI for undo/redo comes with NoorEditor.

### 2026-11-15 — Noor Interchange v0.3
- Seeds are unsigned 64-bit; `license.attribution` for automatic credits; `generator.determinism`
  plus cross-machine determinism rules (Thornwake D5). All optional: v0.2 files stay valid.
- Schema updated; the real BadBuilding manifest in `tests/data` validates against it.

### 2026-11-15 — Milestone 2, first increment: projects, asset registry, GLB import, scenes, CLI
**Added**
- ADR-004 (scene model), ADR-005 (project serialization), ADR-006 (asset registry).
- Dependencies: nlohmann/json 3.12 and cgltf 1.15 (MIT, pinned by hash).
- `foundation`: `fs::read_file` (size-limited), `fs::write_file_atomic` (tmp + replace + `.bak`),
  SHA-256 (FIPS vectors tested), `math::Quat` / `to_mat4` / `compose`.
- `noor::asset`: `Project` (create/open, version check, path-escape protection), deterministic
  JSON helpers, `AssetRegistry` (`.noormeta` sidecars; GUID from the Noor Interchange manifest
  when present; re-attaches metas of files moved outside the engine by content hash; duplicate-id
  and orphan handling; in-engine `move_asset`), `import_gltf` (geometry, base colors, TRS
  hierarchy, `extras.noor`, untrusted-input checks).
- `noor::scene`: entities/components document, byte-stable save/load, unknown components kept,
  `instantiate_model` (markers → `marker`, collision → `collider`, never `mesh`), validation.
- `noor_cli`: `new`, `scan`, `info`, `add-model`, `validate` (headless; runs in CI).
- `noor_diag --glb <file>`: renders an imported model with markers.
- Test data: one real BadBuilding block (`tests/data/badbuilding`, 557 KB GLB + manifest + recipe).
- Tests: 22 new unit tests (incl. the M2 rename/move gate), `cli.project_workflow`,
  `render.glb_building`. 59/59 pass (Debug), 54/54 non-GPU (Release).

**Known limitations**
- Base colors only (no textures); LOD1+ files not loaded; `move_asset` doesn't move Noor
  Interchange side files; no undo/redo, prefabs, hot reload or migrations yet.

### 2026-11-15 — Milestone 1 complete: lifecycle test, live redraw, GPU labels
**Added**
- `Window::minimize/restore/set_size/display_index/move_to_display`, `platform::display_count()`
  (each call waits for the OS with `SDL_SyncWindow`).
- `platform::set_live_redraw_callback`: SDL event watch that redraws during the Windows modal
  resize loop (fixes the Milestone 0 "stretched frame while dragging" limitation).
- `noor_diag --lifecycle-test` and CTest `lifecycle.window`: minimize, restore, two resizes and,
  with 2+ displays, a move to another monitor and back; 11 checks. Passes on the dev machine (2 displays).
- `VK_EXT_debug_utils` whenever available: object names (swapchain, depth, meshes, pipeline,
  command buffers, query pool, capture buffer) and command labels (frame, main pass, capture).
  `MeshData::debug_name`.
- `Renderer::swapchain_size()`.
- `docs/milestones/M1_REVIEW.md`: gate review with evidence.

**Fixed**
- The renderer no longer acquires/presents while the window is minimized (it presented one
  extra frame; found by the lifecycle test).

**Known limitations** (accepted Milestone 1 exceptions)
- Device loss is reported, not recovered.
- High-DPI above 100% untested.

36/36 tests pass (Ninja Debug).

### 2026-11-15 — Noor Interchange v0.2
Feedback from the first real exporter (BadBuilding 6d59174):
- Collision: `extras.noor.collision` is authoritative and never rendered; suffix `-colonly` /
  `-convcolonly` replaces `-col` (Godot draws `-col` meshes).
- LODs: one file per level (`<name>.lod<n>.glb`, schema role `lod` + `lod` number); wrapper
  nodes still allowed.
- Markers: new `passage`; `entrance` (place to get in) vs `door` (openable object) clarified;
  more `entrance` kinds; `door.opens`.
- Ids may be UUID v5 from a natural key; tool-specific manifest blocks; `not_run` validation allowed.
- Schema re-validated: examples pass, v0.1 manifests still accepted, LOD files without a level rejected.

### 2026-11-15 — Milestone 1, second increment: input, fly camera, GPU timing, stress test
**Added**
- `platform/input.h`: keyboard (physical scancodes) and mouse state with pressed/released edges,
  mouse deltas, wheel; released on focus loss. `platform::input()`, `set_relative_mouse()`.
- `render/fly_camera.h`: free-fly camera (yaw/pitch, flattened forward, clamped pitch,
  speed/boost); 7 unit tests.
- GPU timestamps per frame (`FrameStats::gpu_ms`) and CPU record time (`cpu_record_ms`).
- `ErrorCode::DeviceLost` with a plain-language message for lost devices (all Vulkan calls via
  `vk_failure`). Detection and reporting only; recovery not implemented.
- `noor_diag`: ground plane, fly camera controls, `--cubes N` grid, timings in the title and an
  exit summary. Verify mode unchanged (fixed camera, hero cube only).
- Test `stress.cubes_2500`: 2,502 draws/frame for 120 frames with validation. 35/35 tests pass
  (Ninja Debug build).

**Measured** (RTX 3080, 1280×720, 2,502 draws): Release CPU record 0.38 ms without validation,
9.0 ms with validation; GPU 0.06–0.09 ms.

**Known limitations**
- One draw call per object; no instancing yet.
- Minimize/restore and monitor changes still untested; device loss is reported, not recovered.

### 2026-11-15 — Continuous integration
**Added**
- `.github/workflows/ci.yml`: Windows, Debug + RelWithDebInfo builds, all tests except `gpu`-labelled
  ones (runners have no GPU), Avalonia spike build. Vulkan SDK from LunarG's installer, cached.
  Actions pinned to commit SHAs.
- `windows-ninja` CMake preset: CI finds the newest MSVC with `vswhere` and builds with Ninja, so
  it doesn't break when runner images change Visual Studio versions (the first run failed exactly
  that way: `windows-latest` has no VS 2022). Verified locally too: 27/27 tests with the Ninja preset.
- CI badge in the README.

**Known limitations**
- GPU tests run only on developer machines. A software Vulkan driver (e.g. lavapipe) could bring
  the render pixel test to CI later.

### 2026-11-15 — Noor Interchange v0.1 (draft)
**Added**
- `docs/interchange/NOOR_INTERCHANGE.md`: shared export rules for all Noor tools (NoorMD benches,
  NoorShip, NoorLevel, NoorArchitect, ...): GLB + `<name>.noor.json` manifest + recipe; markers,
  parts, LODs and collision in `extras.noor`; ADR-022 space conventions.
- `docs/interchange/noor.asset.schema.json` (JSON Schema 2020-12) and two example manifests.
  Examples validate; wrong units, Z-up, unknown kinds, escaping paths and missing ids are rejected.
- ADR-023 (Proposed).

**Known limitations**
- Draft: no tool exports it yet and NoorEngine has no importer yet (Milestone 2).

### 2026-11-15 — Milestone 1, first increment: meshes, camera, depth, GPU pixel tests
**Added**
- `noor/foundation/math.h`: Vec3/Vec4/Mat4, rotation, look_at, reverse-Z infinite perspective;
  7 unit tests locking in ADR-022 conventions (right-handed, +Y up, column-major, reverse-Z).
- ADR-022 (math and coordinate conventions).
- Build-time HLSL → SPIR-V with DXC (`cmake/NoorShaders.cmake`), embedded into the binary.
- VMA 3.5 for GPU memory.
- Renderer: dynamic rendering, D32 reverse-Z depth buffer, swapchain image views, mesh pipeline
  (lit vertex colors), `create_mesh`/`destroy_mesh` (staged upload, index validation),
  `FrameDesc` with camera and draw list, per-frame stats, frame capture (`request_capture` /
  `take_capture`). Renderer split into `vulkan_renderer.cpp`, `vulkan_resources.cpp`,
  `vulkan_capture.cpp` with a shared internal header.
- `noor_diag`: lit spinning cube, `--verify-render` (pixel probes) and `--screenshot` (BMP).
- Test `render.cube_pixels`: checks background, front face and top face colors in a real frame.
  Verified that it fails when triangle winding is deliberately broken. 27 tests pass.

**Known limitations**
- Mesh upload blocks on the GPU; mesh destruction waits for device idle.
- Single hard-coded shader and light; editor viewport still clears only.
- Not done yet for Milestone 1: input services, GPU timing markers, device-loss handling, stress tests.

## Milestone 0

### 2026-11-15 — Avalonia editor spike (ADR-003 accepted)
**Added**
- `platform::PresentationTarget`: interface the renderer presents to. `Window` (SDL) and the new
  `NativeChildWindow` (Win32 child window inside a foreign parent, e.g. the editor UI) implement it.
  The renderer no longer depends on SDL windows.
- `noor_editor_api.dll` (v0, spike-grade C ABI): log forwarding, viewports (native child window +
  dedicated render thread, live stats). Destroy pumps UI messages while joining the render thread.
- `spikes/avalonia_viewport`: Avalonia 12.1.4 + Dock 12.1.0.13 editor shell with a docked Vulkan
  viewport, live Inspector/Console/status bar, labeled sample panels, and `--smoke` automated checks.
- Devlog #003 with real screenshots.

**Measured** (Debug, validation on, RTX 3080, 60 Hz, 100% scale): 60 FPS; 236 ms viewport → first
frame; 1.6 s process start → first frame; resize reaches swapchain; float into own window re-renders
after 363 ms; 0 validation errors; 0.74% machine CPU; 247 MB working set. Details in ADR-003.

**Fixed during development**
- Spike screenshots first used a screen BitBlt, which captured another application overlapping the
  window. Deleted immediately; capture now uses `PrintWindow`, which only renders our own window.

**Tested by hand** (Tom): hover/click input reaches the viewport (hue follows the mouse, click
brightens); drag-and-drop docking with the mouse works.

**Known limitations**
- Untested: high-DPI >100%, dock drop indicators over the viewport itself (airspace), live
  drag-resize, keyboard focus, Release numbers.
- One Vulkan instance + device per viewport (floating costs ~360 ms); needs a shared device.
- Floating Dock windows show the default blue accent instead of the theme purple.
- `renderer.h` now includes `vulkan.h` indirectly through `presentation_target.h`, weakening
  ADR-002's "no Vulkan types above render/" rule. To be tidied when the RHI layer arrives.

### 2026-11-15 — Devlog website
**Added**
- `website/`: static site (home with the README "Honest status" table, devlog index, one page
  per `docs/devlog/DEVLOG_NNN_*.md`, ADR index, Atom feed, 404). Generated by a small Node script;
  one dependency, `marked` 18.1.2 (MIT), pinned with a lockfile. Dark-first with a light mode.
  Editor mockup shown only with a CONCEPT ART label.
- New devlog files appear on the site automatically; relative links are rewritten to site pages
  or GitHub. 12 unit tests for the build helpers (`npm test` in `website/`).
- `.github/workflows/pages.yml`: tests, builds and deploys to GitHub Pages on push to `main`
  (site, devlog, ADR index, README or workflow changes) and on manual run; PRs build only.

**Known limitations**
- Not yet deployed or run on GitHub Actions at the time of writing. Workflow YAML was not
  checked with a linter locally.

### 2026-11-15 — Repository foundation
**Added**
- Repository layout, MIT license (ADR-019), CONTRIBUTING, IDEAS parking lot, project CLAUDE.md.
- ADRs: 001 C++23/CMake/MSVC (Accepted), 002 Vulkan 1.3 (Accepted), 003 Avalonia editor GUI
  (Proposed, spike pending), 019 MIT license (Accepted), 021 SDL3 + dependency policy (Accepted).
- CMake build with presets; dependencies pinned by commit hash (SDL 3.4.18, doctest 2.5.3);
  `third_party/manifest.json`.
- `noor::foundation`: structured thread-safe logging (console, file, memory sinks; per-level
  counters), `Result<T>`/`Error` error handling, `NOOR_ASSERT` with pluggable handler, v4 GUIDs.
- `noor::platform`: SDL3 init/shutdown, event pump, high-DPI aware window, Vulkan surface bridge.
- `noor::render`: Vulkan 1.3 instance/device/swapchain, 2 frames in flight, sync2 barriers,
  clear-to-color frame loop, swapchain rebuild on resize/out-of-date, validation message capture.
- `noor_diag` diagnostics tool: reports GPU, clears to Noor purple (sRGB-correct), FPS in title.
- Tests: 18 unit tests (log, error, assert, GUID) and a GPU smoke test (180 frames, validation on,
  fails on any validation error). Debug and RelWithDebInfo both pass on the dev machine.

**Fixed during development**
- Vulkan loader errors from third-party implicit layers (ReShade on the dev machine) were counted
  as validation errors. Now reported as `[loader]` warnings; real validation errors still fail.
- Startup no longer builds the swapchain twice (resize event with unchanged size is ignored).

**Known limitations**
- Window resize and maximize tested by hand by Tom (1280×720 → 222×161 → 1920×1017; swapchain
  rebuilt correctly, 1,311 frames, zero validation errors). Minimize/restore and monitor changes
  remain untested. No automated resize test yet.
- No live redraw while dragging a window edge: Win32's modal resize loop pauses our frame loop,
  so the swapchain rebuilds only when the drag ends and Windows stretches the last frame meanwhile.
  Fix before the editor (render from an SDL event watch during resize).
- No negative test yet proving a deliberate validation error fails the smoke test.
- Windows only. No CI yet (GitHub Actions runners have no GPU; unit tests can run there).
