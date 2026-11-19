# Particles (NoorFX)

Milestone 8B, [ADR-035](../decisions/ADR-035-particles-and-sprites.md). **Status: parts 1 and 2 of 8.** Effect
particles are simulated, replay identically everywhere, and are drawn in games. Rain and snow (part 3), the Effects
panel with a live preview in the editor (part 4; until then the editor doesn't draw particles) and scripting (part 6)
come later.

## Two kinds of particles

- **Effect particles** (fire, smoke, sparks, dust puffs, debris, magic) come from an `emitter` component. They are
  simulated on the CPU at a fixed 60 steps a second, from a seed, so the same scene always gives the same particles:
  in the editor, in a built game, in Debug and Release, on any machine.
- **Weather particles** (rain, snow, drifting dust) will come from the environment and cost almost nothing (part 3).

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
add 0.58 ms. The player's `--report` shows `particles` (emitters, alive, fingerprint) and
`timing_ms.particles`.

## Tests
- `particles:` unit tests: sine and cosine, curves, rates, lifetimes, gravity, terminal velocity, wind, shapes,
  bursts, caps, world and local space, the runtime system, and a pinned fingerprint that Debug and Release must
  both give.
- `cli.particles_build_identically`: a campfire (fire, smoke from a `.noorfx`, sparks, a breeze) replays bit for
  bit in a built game.
- Goldens `Particles_Campfire` (noon), `Particles_Campfire_Night` (firelight under the moon), `Particles_Glass` (smoke
  between a pane in front and one behind: the order).
- `render.particles_cost`: the campfire and 10,000 smoke particles within their GPU and CPU budgets.
