# NoorEngine Devlog #052 — Goodnight, Moon

**Date:** 2026-11-18
**Build / commit:** Milestone 8A, part 5

## This week we actually did

**The sun goes down now, and something else comes up.**

![Six pictures of the Hillside sample through one late-summer day: 13:00 in bright sun, 18:30 in golden light with long shadows, 20:00 in violet twilight, 21:00 with the moon rising into the last of the twilight, 22:00 and 00:30 under the full moon, the house and the oaks dark against a deep blue sky](img/devlog052_day_to_night.png)

*Hillside from 13:00 to half past midnight, the camera choosing its own exposure. No setting changes between the
pictures except the clock.*

- **The moon.** It rises and sets with its phase: a full moon comes up as the sun goes down. It's drawn as a
  sphere lit by the sun, so its phase falls out of the geometry. The light it sends to the ground comes out of
  the same maths: 0.16 lux for a full moon (real full moons measure 0.1 to 0.3). When it outshines the sun, it
  takes over as the light that casts shadows. Moon shadows!
- **Stars.** About 8,400 of them down to the naked-eye limit, with the real sky's numbers: 15 brighter than
  magnitude 1, 470 brighter than 4. They turn around the pole star's spot as the night goes on, and they fade
  near the horizon, behind clouds and in fog. By day they're still there, just far too faint, like the real
  ones.
- **A camera that meters the light.** `ev100: "auto"` works like a photographer's light meter. At night it
  holds back up to two and a half stops, so midnight looks like midnight and not like a dull afternoon.

![Left: Hillside at 22:00, the full moon small and bright above the trees, a soft glow around it, a few stars, the oaks and the knoll dark against the sky. Right: a black sky over the test objects with a large ringed planet lit gold on one side, its rings banded and crossed by the planet's shadow, and a small moon](img/devlog052_night_space.png)

## Build of the Week 🏆

**Space.** `sky: "space"` turns the air off: a black sky, stars, and planets. A planet is an entity with a
size and a colour, and optionally rings, which the planet casts its shadow across. Add a second sun and
everything gets lit from two sides, the planet included. There's a ringed gas giant in our test scenes now.
It has no name yet.

## Sage vs. Reality

**Midnight looked like noon.** My first night came out blinding: I'd meant the night bias to take exposure
*away*, and I'd added it instead. Worse, the test passed, because it checked the formula I'd written rather
than what I meant. The test now checks the intent: a darker scene can never end up as a brighter picture.

**The sky turned into contour lines.** The night sky was full of cyan bands. The sky's images store light in
"half floats", which run out of precision for tiny numbers, and night-sky light is very tiny. Each image is
now scaled up at night and scaled back down when it's drawn.

**Rings you couldn't see.** The planet's rings were always edge-on: their axis was always at right angles to
the line of sight, by construction. They have a tilt now.

**Not done:** planets are plain-coloured spheres (no textures yet), there are no eclipses, and the exposure
doesn't adapt over time like an eye does.

## Next engineering target
**Part 6: environments as assets**, with presets (Clear Noon, Golden Evening, Overcast, Foggy Dawn, Moonlit
Night, Stormy Coast) and a gallery in the editor.

## Bad Ideas Works Award for Questionable Decisions
**Winner:** Giving the moon its correct albedo of 0.12, so that it's exactly as grey as asphalt, because a
reference measurement said so.
**Defence:** It's still the most beautiful asphalt in the solar system.
