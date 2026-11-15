# NoorEngine Devlog #002 — The Purple Rectangle

**Date:** 2026-11-15
**Build / commit:** first commit on `main`

## This week we actually did

We wrote code. Real code. Code that compiles with warnings treated as errors, which is the
software equivalent of wearing a tie to your own birthday party.

- A **build system** (CMake) with every dependency pinned to an exact commit.
- A **logging system** that knows the time, the thread, the file and the line, so future bugs
  can no longer claim they were "just passing through".
- **Error handling** that returns errors instead of throwing them across the room.
- **GUIDs**, so that one day, when you rename a texture, your scene won't forget who it was.
- A **window**. It opens. It closes. It knows about high-DPI screens.
- A **Vulkan renderer** that creates a device, a swapchain, and then, with enormous ceremony,
  clears the screen to Noor purple. Correctly in sRGB.
- **19 automated tests**, including one that opens a real window, renders 180 frames with the
  Vulkan validation layers watching, and fails if Vulkan has anything to complain about.

## Build of the Week 🏆

A purple rectangle. 180 frames of it, in 2.99 seconds, at the monitor's 60 Hz vsync, on an
RTX 3080. Zero validation errors.

Before anyone gets excited: that "16.6 ms per frame" is the monitor's refresh rate, not engine
performance. We are measuring the speed at which a purple rectangle can wait for vsync. It is
very good at waiting.

## Sage vs. Reality

On its very first run, the smoke test **failed**. Sage braced for a synchronization bug.

The culprit: ReShade, a third-party graphics overlay installed on the dev machine, which
registers itself into every Vulkan application on the system, then fails to load. Our test
loyally reported it as an error.

The engine was innocent. The fix: loader/environment messages are now logged as `[loader]`
warnings, while real validation errors still fail the test. Lesson learned: your engine runs on
other people's computers, and other people's computers are full of surprises.

## What broke (and why)
- See above. Also two compiler warnings, because MSVC disliked a variable named `info`. MSVC has
  opinions.

## Known limitations
- Tom grabbed a window edge. It stayed purple. Confirmed by human eyes and by the log
  (222×161 to 1920×1017, zero errors). Minimize is still untested.
- While you drag, Windows pauses our frame loop and stretches the last frame. Invisible on a
  purple rectangle, very visible on a forest. On the list.
- Windows only, no CI yet.
- There is no editor. There are no scenes. There is a purple rectangle.

## What Could Possibly Go Wrong
Next up is choosing the editor GUI toolkit by actually building a test viewport inside it,
instead of trusting screenshots. Tom's earlier ImGui experiment has been formally retired with
full honours.

## Next engineering target
- Avalonia + Vulkan viewport spike (ADR-003).
- CI for unit tests on GitHub Actions.
- Milestone 1 groundwork: GPU memory allocation (VMA), shaders, the first real triangle,
  which per spec section 29 is "a valid technical test, not a NoorEngine release".

## Bad Ideas Works Award for Questionable Decisions
**Winner:** Spending an entire day to make a window purple.
**Defence:** It's the most thoroughly tested purple in the industry.

---
*Bad Ideas Works — Code shall remain understandable to whichever AI inherits this mess.*
