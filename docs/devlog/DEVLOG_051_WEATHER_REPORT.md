# NoorEngine Devlog #051 — Weather Report

**Date:** 2026-11-18
**Build / commit:** Milestone 8A, part 4

## This week we actually did

**Clouds. Two layers of them, drifting.**

![Left: the test objects under a late-afternoon sky with grey-gold broken clouds and blue gaps, the clouds smoothing out towards the horizon. Right: the same objects under a full white overcast deck, the sun only a bright glow, the objects lit softly with no shadows](img/devlog051_clouds.png)

*Left: broken cumulus with a thin high veil, late afternoon. Right: overcast. Same objects, same sun; under the
overcast the shadows are gone and the light comes from the whole grey sky.*

- **Layers of cloud.** Up to two per sky: a heavy lower deck and a thin veil high up, each with its own
  coverage, thickness, feature size, wind and seed. The same seed and the same game time always give exactly
  the same sky. The tests check it, and the golden pictures depend on it.
- **Lit by the sky they're in.** Clouds are lit by the sun as it reaches their height (so at sunset they glow
  orange while the ground is already dim) and by the blue sky above them. Thin edges catch the light, thick
  bodies go grey.
- **Cloud shadows.** Every point of the scene checks how much cloud sits between it and the sun, so shadows
  drift across the ground with the wind. Under a full overcast almost no direct sunlight gets through, the
  shadows fade away, and the grey sky lights everything. That one happened for free: the scene's ambient
  light is built from the cloudy sky.

## Build of the Week 🏆

**26 microseconds.** That's what two layers of clouds cost per frame, cloud shadows included. The budget was
500. And when the clouds drift, the scene's reflections and soft light don't need rebuilding every frame:
they follow the clouds every few seconds, which turned 300 rebuilds in 5 seconds into 6.

## Sage vs. Reality

**The horizon was full of static.** The first clouds had a grainy band near the horizon, where a single pixel
covers kilometres of sky and the finest ripples of the noise turn into noise of a different kind. Now the
fine detail fades out as a pixel gets bigger than it, which is what a camera would see anyway.

**Charcoal clouds.** The first cloud bodies were nearly black. Real clouds glow inside, because light bounces
around in them, and their bases catch light reflected up from the sunlit ground. Two small terms later they
look like weather, not smoke.

**The GPU was asleep.** The timing tests started failing at random. The reason was the graphics card: it
sat at 210 MHz (its top is 2115) because our little test scenes don't wake it, while other programs
shared it. The same measurement came out 3-10× different from one run to the next. The tests now
judge each run's *fastest* frame, because a sleepy or busy GPU only ever adds time. They print the slow
numbers too.

**Not done:** the clouds are thin layers, not volumes. You can't fly into one, they're seen from below only, and
they don't shade each other. Volumes are a later phase.

## Next engineering target
**Part 5: night and space.** The moon, the stars, exposure that copes with night, and skies with no air at all.

## Bad Ideas Works Award for Questionable Decisions
**Winner:** Spending an evening making sure that a cloud ten kilometres away doesn't flicker, on behalf of
a red box that will never look up.
**Defence:** The Yellow Dude might.
