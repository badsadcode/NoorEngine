# Particles (NoorFX)

Milestone 8B, [ADR-035](../decisions/ADR-035-particles-and-sprites.md). **Status: parts 1-4 of 8.** Effect particles
are simulated, replay identically everywhere, drawn in games and previewed in the editor; rain, snow and dust come
from the environment; the Effects panel edits them. Scripting comes with part 6.

## Two kinds of particles

- **Effect particles** (fire, smoke, sparks, dust puffs, debris, magic) come from an `emitter` component. They are
  simulated on the CPU at a fixed 60 steps a second, from a seed, so the same scene always gives the same particles:
  in the editor, in a built game, in Debug and Release, on any machine.
- **Weather particles** (rain, snow, drifting dust) come from the environment and cost almost nothing (below).

## The `emitter` component

Every field is optional (the comments are for this page; the file is plain JSON).

```jsonc
"emitter": {
  "rate": 20,                                    // particles a second
  "bursts": [{"time": 0.2, "count": 12, "interval": 0.5, "cycles": 0}],  // 0 cycles with an interval: forever
  "duration": 0, "loop": true,                   // 0: runs forever; a one-shot sets a duration and loop false
  "max_particles": 1000, "seed": 1,
  "shape": {"type": "cone", "angle": 15, "radius": 0.25},  // point | sphere | box | cone; "surface": true for shells
  "lifetime": [0.4, 0.8], "speed": [0.8, 1.6],   // a number, or [min, max]
  "gravity": -0.3,                               // x 9.81 m/s^2 down (negative rises, like hot air)
  "drag": 1.0,                                   // per second: pulls the velocity towards the wind's
  "wind": 1,                                     // how much of the scene's wind
  "space": "world",                              // or "local": particles travel with the entity
  "size": [[0, 0.25], [1, 0.05]],                // metres over life
  "color": [[0, "#FFE0A0"], [0.5, "#FF7020"], [1, "#801000"]],  // sRGB, blended in linear light
  "alpha": [[0, 1], [1, 0]]
}
```

**Shapes**, in the entity's own axes: a **point** emits in every direction. A **sphere** emits outwards from inside
it (or from its surface). A **box** emits along +Y from inside it (or its faces). A **cone** emits along +Y within
`angle` degrees, from a disc of `radius` at its base.

**Forces:** gravity, and drag towards the air. With drag, particles settle at a terminal velocity (gravity / drag),
and the wind carries them along. Without drag, the wind does nothing, which suits sparks and debris.

## How they're drawn: `render`

```jsonc
"render": {
  "blend": "alpha",          // alpha (smoke, dust) | additive (fire, sparks, magic: adds light) | premultiplied
  "facing": "camera",        // camera | velocity (stretched along their motion: sparks, rain)
  "stretch": 0.04,           // velocity facing: the streak covers this many seconds of motion
  "lit": true,               // lit by the sun (its shadows, the clouds') and the sky; additive defaults to false
  "emission": 0,             // light of its own, in nits per unit of colour (a campfire's flames: thousands)
  "texture": "<id>",         // optional; without one, a soft round dot
  "frames": [4, 4],          // a flipbook grid in the texture
  "frame": "life"            // life (plays over each particle's life) | random (one frame each)
}
```

Each emitter is one instanced draw in the see-through pass, sorted by distance together with see-through meshes, so
smoke behind a window is tinted by it and smoke in front of it isn't. Alpha particles are sorted back to front within
their emitter. Particles are fogged and darkened by aerial perspective like everything else.

**Exposure and glowing particles:** the automatic exposure (8A) meters the sun, the sky and the moon, not lamps or
glowing particles. At night it exposes for moonlight, so a fire bright enough for noon blows out to white. Give a fire
a point light for its glow, and a lower `emission` for night scenes (the test scene uses 2.5 nits at night and 8,000
at noon). A meter that sees local light is noted in the ideas inbox.

## Rain, snow and dust

Three numbers on the environment, 0 to 1:

```json
"rain": 1.0, "snow": 0.0, "dust": 0.0, "wind_speed": 5, "wind_direction": 60
```

At 1 there are 200,000 raindrops, 60,000 snowflakes or 20,000 motes of dust around the camera. Rain streaks along its
fall (9 m/s) and the wind, snow drifts down (about 1 m/s) swaying, and dust floats and glints when you look towards
the sun. They're lit by the sky and the sun (with shadows) and fade into fog like everything else.

**How it's cheap:** nothing is simulated or stored. Each drop's place is worked out in the vertex shader from its
number, the time and the wind, inside a box that follows the camera and wraps round (a drop falling out of the bottom
comes back at the top). Drop number *i* exists while *i* is below intensity × the most drops, so more rain adds drops
without moving the others, which is why weather zones can start the rain smoothly: blend `"rain": 1` into a zone like
fog.

**Not yet:** drops don't stop at roofs (it rains indoors), snow doesn't settle, and puddles don't form. Rain is drawn
after see-through things, so a drop behind a window isn't tinted by it.

## The Effects panel (editor)

**Window ▸ Effects** (a tab in the bottom dock), like the Environments gallery:

- **Presets:** Fire, Smoke, Sparks, Dust, Rain splash and Magic, built into the engine and never changed. **Add to
  scene** copies one into `Assets/Effects/` (once) and puts it on a new entity; **Use on selected** puts the selected
  effect on the selected entity. Both are one undo step.
- **The module stack:** the selected project effect's settings, grouped as Spawn, Shape, Life and speed, Forces, Over
  life (curves for size and alpha, a gradient for colour, with a small plot and swatches) and Render. Every change is
  saved at once, and the viewport's preview follows it.
- **The preview:** while you edit, the viewport runs every effect in the scene on its own clock. **Solo** shows only the
  selected entity's effect, **Mute** hides it, **Restart** starts them all from nothing, **Pause** and **Play** freeze and
  run them. The dice button rolls a new seed (the same effect, other particles).

The editor's preview is its own simulation of the scene, not the game's: in Play mode, the game's particles are the
ones that count (and replay identically in built games).

## Collisions, sub-emitters and budgets (part 7)

**Collisions** stop particles on the ground and on planes:

```json
"collide": {"ground": true, "planes": [{"y": 0}, {"point": [0, 0, -5], "normal": [0, 0, 1]}],
            "bounce": 0.3, "friction": 0.2, "radius": 0.02, "kill": false, "hit_speed": 0.5}
```

- `ground`: the scene's terrains, the very surface they're drawn and collided with, so particles rest on the hills
  you see, never inside them. Off every terrain there's no ground.
- `planes`: up to 8, in world space; `{"y": 0}` is a floor at that height.
- `bounce`: how much of the speed into the surface comes back (0 sticks, 1 bounces fully); `friction`: how much of the
  speed along it is lost each hit; `radius`: how far from the surface they stop.
- A hit faster than `hit_speed` (m/s into the surface) is an *event*: with `kill` the particle ends there (raindrops),
  and it can start a sub-emitter. Resting and rolling don't count as hits.
- Not yet: other physics objects (boxes, models) don't stop particles.

**Sub-emitters** start new particles where others hit or die:

```json
"sub_emitters": [
  {"on": "hit", "count": 4, "inherit_velocity": 0, "emitter": {"lifetime": 0.3, "speed": [1, 2], "gravity": 1,
                                                               "shape": {"type": "cone", "angle": 35}}},
  {"on": "death", "count": 1, "emitter": {"asset": "<.noorfx id>", "rate": 0}}
]
```

Each is an emitter of its own (world space; no steady rate unless given), bursting `count` particles at every event,
with `inherit_velocity` times the parent particle's velocity added. Up to 4 per emitter, one level deep (a
sub-emitter's own sub-emitters are ignored). Its `emitter` can point at an effect asset, with overrides. In the editor
they follow their entity: Solo and Mute act on both.

**Budgets** keep a busy scene in check:

- each emitter's `max_particles` (as before);
- the scene's: at most 100,000 effect particles at once. Spawning is handed out in scene order, so the same scene holds
  back the same particles every run; the player's report says how many (`particles.throttled`);
- `render.max_distance`: an effect whose particles are all further than this from the camera isn't drawn. Only
  drawing: the simulation never depends on the camera, so replays stay identical.

Measured (RTX 3080): 50 spark fountains asking for 150,000 particles keep to 98,750, cost 0.18 ms of GPU and simulate in
1.0 ms with their collisions (`render.particles_cost`).

## From scripts

`e:burst(count)` (Lua) and `Entity.Burst(count)` (C#) spawn that many particles from the entity's emitter at the next
fixed step, as deterministic as the rest (Milestone 8B, part 6). An emitter with `"rate": 0` then only makes what
scripts ask for: a hit, a pickup. Settings are changed with `set("emitter", ...)` / `Entity.Set`: the particles
already flying carry on, and new ones follow the new settings.

## Wind

The scene's environment can set a ground wind: `"wind_speed"` in m/s and `"wind_direction"` in degrees, where it
blows towards (0 = -Z, 90 = +X, like the clouds'). Weather zones blend it like any other setting, the short way
round the compass.

## Effect assets: `.noorfx`

An effect can live in its own file so it can be placed many times:

```json
{"noor_fx": 1, "name": "Smoke", "emitter": {"rate": 8, "lifetime": [2.5, 4], "gravity": -0.08, "drag": 0.9}}
```

An emitter points at it with `{"asset": "<id>"}`. Any field written beside it overrides the file's, so
`{"asset": "...", "rate": 10}` is that smoke, a bit thicker.

## Cost

Simulation: 10,000 particles step in 0.02 ms (Release) and 0.34 ms (Debug). Drawing (RTX 3080, 1280x720, the fastest
of 300 frames): the campfire test scene (about 100 particles) adds 0.005 ms; 10,000 big overlapping smoke particles
add 0.58 ms. Rain at full strength (200,000 drops) adds 0.18 ms; snow is too cheap to measure. The player's
`--report` shows `particles` (emitters, alive, fingerprint) and
`timing_ms.particles`.

## Tests
- `particles:` unit tests: sine and cosine, curves, rates, lifetimes, gravity, terminal velocity, wind, shapes,
  bursts, caps, world and local space, the runtime system, and a pinned fingerprint that Debug and Release must
  both give.
- `cli.particles_build_identically`: a campfire (fire, smoke from a `.noorfx`, sparks, a breeze) replays bit for
  bit in a built game.
- Goldens `Particles_Campfire` (noon), `Particles_Campfire_Night` (firelight under the moon), `Particles_Glass` (smoke
  between a pane in front and one behind: the order).
- `render.particles_cost`: the campfire and 10,000 smoke particles within their GPU and CPU budgets, and 200,000
  raindrops within 0.5 ms.
- `cli.weather_walk`: walking into a weather zone that rains starts the rain smoothly (0 to 1, never back, at most
  1.25% of the way a frame), the same in a built game.
- Goldens `Hillside_Rain` and `Hillside_Snow` (`tools/samples/add_hillside_weather.py`).
- `noor_editor_api_tests` "effect api": six presets that all run; apply (a copy, on a new or existing entity, one undo
  step); editing a copy changes what a game simulates and never the preset; reset, duplicate; the preview's controls.
- The editor smoke test: Add to scene, the viewport previewing it, an edit reaching the preview at once and the file
  identically, the preset unchanged, Mute, undo.
