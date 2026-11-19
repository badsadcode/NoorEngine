# ADR-035: Particles and sprites

**Status:** Accepted (Milestone 8B; parts 1-4: effect particles simulated and drawn, weather particles, the Effects panel, 2026-11-19)

## Context
NoorEngine has no particles and no sprites. 8A's weather needs rain and snow; The Uncounted, Chapter Two (the
first game planned on the engine) draws its creatures as 8-direction billboard sprites and uses particle effects
throughout. The spec's NoorFX section (17.4) asks for emitters, shapes, curves, collisions, force fields, trails,
ribbons, mesh particles, GPU simulation where viable, events, parameter binding and an editor with a reproducible
seed. Everything else in the engine is deterministic (procedural graphs, weather zones, the sky), and tests
compare built games with projects frame by frame and pixel by pixel; particles must not be the exception.

## Options
1. **One GPU-simulated particle system for everything** (compute shaders keep each particle's state). It scales to
   millions, but determinism is hard (the order atomics hand out slots in varies between runs and GPUs), CPU-side
   gameplay (a spark that triggers something, a script reading a particle) needs read-backs, and there's no
   reference to test against.
2. **One CPU-simulated system for everything.** Deterministic and easy to test, but rain over a whole valley is
   hundreds of thousands of drops: too many to update and upload every frame.
3. **Two kinds, by job:** effect particles simulated on the CPU (deterministic, testable, a few thousand per
   effect), and weather particles with no state at all, each worked out from its index and the time in the vertex
   shader.

## Decision
Option 3.
- **Effect particles:** an `emitter` component (inline settings, or a `.noorfx` asset with overrides, like
  environments) simulated at a fixed 60 Hz step with a seeded random stream per emitter (the procedural graph's
  approach), in the runtime's update. Spawning (rate, bursts, shapes), lifetime, velocity, gravity, drag, wind;
  curves over life for size, colour and alpha. The C++ simulation is the reference and the runtime; if measuring
  ever says it's too slow, a GPU version is checked against it.
- **Weather particles:** a camera-following box that wraps, so it never runs out of drops. Each drop's position
  comes from a hash of its index and the seed, plus fall speed and the clouds' wind times time, wrapped into the
  box. Drawn instanced, with no per-drop memory and no simulation. The environment sets them (`precipitation`:
  rain, snow or dust, with an intensity), so weather zones blend them in like fog.
- **Drawing:** instanced billboards through ADR-032's batching (facing the camera, or stretched along their
  velocity), flipbook frames from a texture, alpha, additive or premultiplied blending, lit by the sun and the live
  sky's ambient, fogged, sorted per emitter back to front in the see-through pass.
- **Sprites:** a `.noorsprite` asset (texture, frame grid, named animations, optional 8-direction rows) and a
  `sprite` component drawn as an alpha-cutout billboard (depth-writing, shadow-casting, upright or fully facing;
  nearest or linear filtering). The row is picked from the angle between the entity's forward and the camera.
- **Scripts:** play/stop/speed and an end event for sprite animations; burst/start/stop and parameters for
  emitters (Lua and C#, one API).

## Consequences
- **Part 2, drawing:** `particle.hlsl` builds each quad from the vertex index (no vertex buffer for corners), one
  instanced draw per emitter in the see-through pass, merged with see-through meshes by distance. Premultiplied
  blending does alpha and additive with one pipeline (additive writes alpha 0); rgb-only writes keep the HDR image's
  alpha for the tonemapper. Its push constants have the mesh shader's shape, so it shares the mesh pipeline layout and
  descriptor sets. The frame's shared shader code (uniforms, shadows, environment light, atmosphere and fog) moved out
  of mesh.hlsl into `frame.hlsli`, unchanged (every golden identical). Cost: a campfire 0.005 ms, 10,000 big smoke
  particles 0.58 ms (RTX 3080, 1280x720).
- **Part 3, weather:** `weather.hlsl`, no vertex data: drop i is a hash of i and the seed, carried by its fall and
  the wind over time, wrapped into a box round the camera; it exists while i < intensity x the most drops (rain
  200,000, snow 60,000, dust 20,000), so intensity changes add drops without moving others and weather zones blend
  rain smoothly. Everything per drop (place, sun shadow, sky light, air) is worked out in the vertex shader, so the
  frame set's shadow, sky-light and air bindings became visible to vertex shaders too. Drawn after the see-through
  things. Cost: 200,000 drops 0.18 ms (budget 0.5). The environment's `rain`, `snow` and `dust` blend from 0 like
  fog; `wind_speed` from 0 too (a zone that doesn't mention wind is calm when fully on).
- **Part 4, authoring:** six built-in effect presets (never written; applied as project copies that remember their
  preset), editor API v25 (`noor_effect_presets`, `noor_doc_effect`: list, read, apply, duplicate, reset, save,
  preview), and the Effects panel (gallery and a module stack saved as you edit). The emitter bookkeeping moved into
  `fx::EmitterSet`, shared by the runtime system and the editor's preview, and the emitter-to-draw conversion into
  `game::particle_draws`, shared by games and the viewport. The viewport previews the scene's emitters on its own
  60 Hz clock from the document's snapshot (settings resolved, an edited `.noorfx` re-read at once), with solo, mute,
  restart and pause: a preview, not the game's simulation.
- **Part 5, sprites:** a sprite is an ordinary mesh draw, not a particle: one shared quad and a cutout material per
  frame whose texture transform picks the frame (`uv_scale`/`uv_offset`), so sprites are lit, shadowed, depth-tested
  and fogged by the existing mesh path with nothing new in the frame. Materials gained `nearest_filter` (a second,
  immutable sampler at binding 3 of the texture set, chosen by flag bit 8; every golden unchanged). The quad's normal
  leans 30 degrees up, so a card facing away from the sun still takes the sky's light. Mirroring (`flip_x`) mirrors
  the texture transform, not the quad: a negative scale turns the quad's front face away and the lighting with it.
  `game::SpriteLibrary` (sheets, the quad, materials per frame, tint and flip) is shared by games and the viewport;
  billboards are turned to the camera every frame (in the viewport too, from the snapshot's `sprites`). Editor API
  v26: `noor_doc_sprite` (list, read with the sliced frames, create from a texture, save, apply). Ranges in
  animations are capped at 65,536 frames (a typo mustn't allocate billions).
- **Found in part 2:** automatic exposure meters sun, sky and moon only, so glowing particles that suit noon blow out
  at night. Scenes give fires a point light and a night emission; a meter that sees local light is in the ideas inbox.
- **Part 1, measured:** 10,000 effect particles step in 0.022 ms (Release) and 0.34 ms (Debug): the CPU budget
  (1 ms) has room to spare, so the GPU question can wait. A pinned fingerprint is the same in Debug and Release;
  a built game replays a campfire bit for bit (`cli.particles_build_identically`). Deterministic maths: own
  sine/cosine (MSVC may swap the library's for vector versions with other rounding in optimised code), drag by
  division (no exp), SplitMix64 random streams seeded per emitter from its seed and its entity's id.
- Determinism everywhere: an effect replays identically from its seed, and so does the weather, so particle
  goldens and built-game comparisons work like the rest.
- Two code paths to keep: the effect simulation (CPU) and the weather maths (shader). They share the drawing.
- Effect particles are bounded by CPU time: the plan's budget is 10,000 particles in under 1 ms, measured in part 1.
- Weather drops don't collide: rain doesn't stop under a roof. A depth- or height-based cut-off is a later step
  (splashes on the ground come from effect particles, part 7).

## Unresolved
- Soft particles (fading where smoke meets the ground) need the scene's depth in the see-through pass: later.
- Whether effect particles move to the GPU at all: only if a measured scene asks for it.
- The effects node graph (spec 17.4): after 8B; the module stack covers 8B's effects.
