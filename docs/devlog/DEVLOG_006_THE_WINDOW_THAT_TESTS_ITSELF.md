# NoorEngine Devlog #006 — The Window That Tests Itself

**Date:** 2026-11-15
**Build / commit:** Milestone 1 complete

## This week we actually did

**Milestone 1 is done.** The spec's checklist for it was simple to read and annoying to prove:
the window must survive being resized, minimized, dragged to another monitor and closed, a real
mesh must render reliably, and the diagnostics must be useful. Here's how we proved each part
without asking Tom to minimize a window forty times:

- **The window now tests itself.** A new automated test minimizes the window, checks that
  rendering stops, restores it, checks that rendering resumes, resizes it twice, and, on a
  machine with two monitors (Tom's), carries it to the other screen and back. Eleven checks, six seconds.
- **No more smeared picture while you drag the window edge.** Windows pauses programs while you
  resize their window. The engine now gets a callback from inside that pause and keeps drawing.
- **RenderDoc speaks our language.** Instead of "Buffer 139", the graphics debugger now shows
  "Cube vertices", "Depth buffer (D32, reverse-Z)" and "NoorEngine frame 312 → Main pass".

The full review, with every requirement and its evidence, is in `docs/milestones/M1_REVIEW.md`.

## Build of the Week 🏆
`lifecycle.window`: a test that grabs the window and throws it around the desktop like an
impatient user, then reports **11 PASS, 0 FAIL**.

## Sage vs. Reality
On its first run the new test reported three failures. Two were bugs in the test: resizing now
triggers an immediate redraw, so the rebuild happened before the test took its "before" picture.

The third was real. After minimizing, the engine still drew **one more frame** into the invisible
window, because the graphics driver didn't object. Wasted work, and exactly the kind of bug nobody
ever notices by hand. Now the engine checks the window size first and doesn't even start a frame
when there's nothing to see.

## What Could Possibly Go Wrong
If the graphics driver crashes (a "device lost"), NoorEngine explains what happened clearly and
exits cleanly, but it doesn't recover by itself yet. That needs the asset system to know where
everything came from so it can reload it. We wrote this down as an official exception instead of
pretending.

## Known limitations
- No automatic recovery from GPU crashes (planned with the asset system).
- Display scaling above 100% hasn't been tested, because nobody here owns a 4K laptop. Yet.

## Next engineering target
**Milestone 2: projects, assets and scenes.** Save things, load things, and import the GLB files
from our Noor generators, starting with BadBuilding's buildings.

## Bad Ideas Works Award for Questionable Decisions
**Winner:** Writing a program whose job is to annoy a window.
**Defence:** The window is now the most thoroughly tested window in the studio. It has earned its frame.

---
*Bad Ideas Works — Code shall remain understandable to whichever AI inherits this mess.*
