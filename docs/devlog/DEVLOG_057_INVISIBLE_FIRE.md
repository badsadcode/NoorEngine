# NoorEngine Devlog #057 — Invisible Fire

**Date:** 2026-11-19
**Build / commit:** Milestone 8B, part 1

## This week we actually did

**The sky group is done, so things can start flying through it.** Milestone 8B is particles and sprites: rain and
snow for the weather we just built, effects, and the 8-direction sprite creatures Tom's game *The Uncounted* needs
for its second chapter. The [plan](../milestones/M8_PLAN.md#8b-particles-and-sprites) has eight parts.
[ADR-035](../decisions/ADR-035-particles-and-sprites.md) splits particles in two:

- **Effect particles** (fire, smoke, sparks) are simulated properly, a few thousand at a time.
- **Weather particles** (rain, snow) aren't simulated at all. Each drop's position is worked out from its number and
  the time, so hundreds of thousands cost next to nothing. That's part 3.

Part 1 is the engine room for effect particles: an `emitter` component, effect files (`.noorfx`) you can reuse with
overrides, four spawn shapes, bursts on a timer, gravity, drag and wind, and size and colour over a particle's life.
The environment now has a ground wind, so smoke drifts the way the breeze blows.

**And you can't see any of it.** Drawing is part 2. Today's campfire is 85 particles of fire, smoke and sparks that
exist only as numbers. Hence the title.

## Build of the Week 🏆

**The same fire, bit for bit, everywhere.** Every particle comes from a seeded random stream, steps at a fixed 60 Hz,
and uses maths that gives the same bits on any machine. The test campfire runs 300 frames in the editor's project
and in a built game, and a fingerprint of every particle's position, speed and age matches exactly. A pinned
fingerprint is the same in Debug and Release. That's what lets particles have golden pictures from part 2 on.

## Sage vs. Reality

**Even sine isn't safe.** The Debug and Release builds of the same code can give different answers for `sin()`.
The optimiser may replace the library's sine with a vectorised one that rounds differently. Particles use their own
sine and cosine (plain + − × only), and drag uses division instead of `exp`. It sounds paranoid, but the pinned
fingerprint now agrees in both builds.

**Faster than budgeted, by fifty times.** The plan allowed 1 ms for 10,000 particles. They step in 0.022 ms. So
effect particles stay on the CPU, where they're easy to test, until a real scene says otherwise.

**A bug from last week, found by this week.** Wind has a direction, and weather zones blend environments. Blending
350° into 10° should pass through north, not swing round through south. Teaching the blender about the new wind
showed it had never known the clouds' wind direction was an angle either. Fixed, with a test.

**My own test crashed.** It looped over the particles of a temporary emitter that C++ had already thrown away. Same
mistake as last week, different file. The engine was fine.

**A Debug build that wouldn't overwrite its own files.** The compiler hit "permission denied" on object files from
yesterday, every time and on different files, though they weren't locked and deleting them worked. Deleting the 192
stale ones and building fresh got around it. The cause is still a mystery.

## Next engineering target
**Part 2: drawing particles.** Billboards that face the camera or stretch along their speed, flipbooks, blend modes,
lit and fogged, sorted. The campfire gets its first picture: one at noon, one at night.

## Bad Ideas Works Award for Questionable Decisions
**Winner:** Shipping a campfire you can't see.
**Defence:** It passes every test. Nobody has ever been burnt by it.
