# NoorEngine Devlog #062 — Coth Turns Around (Eight Times)

**Date:** 2026-11-20
**Build / commit:** Milestone 8B, part 6

## This week we actually did

**Meet Coth.** He's from The Uncounted, Tom's RPG: barefoot, beard to his belt, sword in hand, and firmly convinced
he spent a thousand years lost in the woods (it was three days). Tom painted him from eight directions, and as of
today NoorEngine can show all of them:

![Eight copies of Coth, a long-haired, bearded, barefoot man with a short sword, standing in a ring on grass: the one in front faces the camera grinning, the one at the back shows his back, the ones at the sides are in profile, and a ninth Coth lies on the ground to the right](img/devlog062_coth_ring.jpg)

*Eight Coths in a ring, each facing outwards, so the camera sees every view at once. The ninth is lying down, which
looks the same from every side (that's the point).*

- **8-direction characters.** A sprite sheet can now say `"directions": 8`. Each column is a view: facing you, then
  turning step by step to your left, away, to your right. That's the same layout The Uncounted uses, so its sheets
  work as they are. The engine works out which view to show from the angle between where the character faces and
  where the camera is.
- **Scripts in charge.** Lua and C# can play an animation, stop it, change its speed, and get told when a
  non-looping one has finished: `on_animation_end("attack")`. That's how a character attacks and then goes back to
  standing, or falls over properly instead of in the middle of a swing.
- **Effects on demand.** Scripts can now burst an emitter: 25 sparks when a sword hits, exactly when it hits.

## Build of the Week 🏆

**The same fall, to the bit, three times.** The test walks Coth round a circle for two seconds, has him attack, and
when the attack has ended, he falls down. It runs as a project, as a built game from Lua, and as a built game from C#.
All three report that he started falling at 2.1666666666666639 seconds. Not "about the same": the same number.

Another test walks him round a circle one frame at a time and checks every frame shows the right view: all eight
directions, in order, one step at a time.

## Sage vs. Reality

**I lost a fight with my own test.** I wrote that a character facing +Z, seen from the side, shows his right side. The
engine said left. The engine was right: standing on +X looking at the origin, your right hand points to −Z, so he's
facing your left. Coth was fine; the test needed fixing.

**The runtime couldn't reach the particles.** The script API lives in the runtime, and the particle system depends on
the runtime, not the other way round. So the World gained a small command channel: a system says "I handle
`emitter.burst`", and scripts ask by name. No new tangles between the modules.

**Animations live in the component.** When a script plays "attack", it writes down when it started and how fast, right
in the sprite component. Drawing, saving and built games all read the same few numbers, so nothing can quietly drift
apart.

**Not done:** events in the middle of an animation (a footstep on frame 2), and sheets that draw five views and
mirror the rest.

## Next engineering target
**Part 7: gameplay particles.** Sparks that bounce off the ground instead of falling through it, splashes when rain
lands, and budgets so a hundred campfires don't melt the GPU.

## Bad Ideas Works Award for Questionable Decisions
**Winner:** A test whose only job is to make Coth attack and fall over, again and again, in two programming languages.
**Defence:** He's been through worse. A thousand years in the woods, apparently. And he still won't wear trousers.
