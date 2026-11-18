# NoorEngine Devlog #040 — Remember Everything

**Date:** 2026-11-18
**Build / commit:** Milestone 7, part 2

## This week we actually did

**The procedural graph learned to remember, to share the work, and to run without the editor.**

- **A cache.** Every node's result is stored under its fingerprint: everything the result depends
  on, boiled down to one number. Change one setting and only that node and the ones after it run
  again. The test counts: three run, three come from the cache. The cache lives on disk, so it
  survives closing the editor.
- **Threads.** Nodes run side by side as soon as their inputs are ready, with progress reports
  and a Cancel button's worth of plumbing. One thread or eight: same bytes, same order. A test checks.
- **Cancel means cancel.** Stop a run halfway and nothing half-done is kept: no half-written files,
  no half-finished cache entries. Run it again and it picks up the nodes that did finish.
- **`noor_cli generate`.** The same graph from the command line, writing its outputs plus a small
  "what made this" file. It writes exactly the same bytes as the editor, and a test compares them
  file by file.
- **Files other tools can read.** A generated height field is a `.noorfield`, a tiny documented
  format. Seven lines of Python read the test terrain back: 257 × 257 samples, 0.32 to 16.5 metres.

## Build of the Week 🏆

**Cancelled, then resumed.** The test stops a run after two nodes. The output folder stays empty,
and the cache holds exactly those two results. The next run takes those two from the cache, computes
the other four, and ends at the same pinned fingerprint as a run that was never interrupted. That's
the whole part in one test.

## Sage vs. Reality

**A quiet week.** All eleven graph tests passed on their first build, which is suspicious in its own
way. So I checked that the "editor equals CLI" comparison really compared something, instead of
skipping because a setting was missing. It did: 30 checks, byte for byte. The one thing I removed
was my own test line that asked "is a second run refused while the first is still going?". The answer
depended on how fast the first run was. A test that passes or fails on timing isn't a test.

**Honest limit.** The cache has no size limit yet. It's derived data you can delete at any time, but
a big terrain graph could grow it. That's written down in the plan, not forgotten.

## Next engineering target
**Part 3: terrain as a client.** A terrain whose base comes from a graph, masks from slope and
height, and the milestone's region promise: move something in one corner, and only that corner's
tiles are recomputed.

## Bad Ideas Works Award for Questionable Decisions
**Winner:** Giving a procedural graph a perfect memory.
**Defence:** It still forgets things on purpose. You just have to delete the right folder.
