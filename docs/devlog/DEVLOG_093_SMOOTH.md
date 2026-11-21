# NoorEngine Devlog #093 — Smooth

**Date:** 2026-11-21
**Build / commit:** Gameplay foundations, part 4: tweens

## This week we actually did

**Things can move nicely without code that runs every frame.** A door sliding up, a light fading out, a sign blushing
red, a platform going back and forth: one line each.

```lua
door:tween("position", vector.create(0, 3, 0), 0.5, "ease_out")
lamp:tween("light.intensity", 0, 2.0, "sine_in_out")
platform:tween("position", vector.create(5, 0, 0), 2, "ease_in_out", { loops = -1, yoyo = true })
```

- **Anything with numbers:** position, rotation, scale, and any number or colour in a component (a light's brightness,
  a camera's zoom, a sprite's colour).
- **22 easings:** the gentle ones (ease in, ease out), and the bouncy ones: `back_out` overshoots and settles,
  `elastic_out` wobbles, `bounce_out` lands like a ball.
- **Loops, back and forth, delays,** and something to do when it's finished (`on_done`). Inside a `noor.start` sequence,
  `t:wait()` waits for it. C# gets the same.
- Tweens follow the game's clock, so they **pause with the game** and run slower in slow motion.

## Build of the Week 🏆

**The chain.** A door rises; when it's up it grows with a bounce; when that's done it turns red. Each step starts from
the one before's `on_done`. Meanwhile a platform swings back and forth forever. Lua in the project, Lua in a built game
and C# in a built game all end up at exactly `door y 3.000, scale 1.500, colour #FF0000, platform x -0.197`.

**Every easing is checked against its formula**, including the ones that misbehave on purpose: `back_in` dips below
zero before it starts, `back_out` overshoots past one.

## Sage vs. Reality

**Half a second, again.** Part 2's floating-point lesson came back: thirty steps of 1/60 s make 0.49999999999999994,
so a half-second tween finished one frame late. Same fix as for timers.

**Halfway between black and orange.** My test expected the colour `#804000` halfway to `#FF8000`. The engine said
`#7F4000`. Which is right? 255 × 0.5 = 127.5. Rounding goes up at exactly half, but "halfway" after thirty frames is
0.49999…, which rounds down. The engine was right; my test was too exact.

## Alden discovers ...

**... `bounce_out`.** Alden tweening his own scale to 1.5 with a bounce, then looking down at his new size, wobbling.
