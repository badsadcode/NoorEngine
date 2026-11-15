# NoorEngine Devlog #008 — Ctrl+Z for a City

**Date:** 2026-11-15
**Build / commit:** Milestone 2, second increment

## This week we actually did

Three things every real editor needs, and nobody notices until they're missing:

- **Undo and redo.** Every change to a scene (moving, renaming, deleting, re-parenting, editing a
  component) is now a command that knows how to take itself back. Several changes can be grouped
  into one undo step, and the engine knows whether a scene has unsaved changes, even when you undo
  your way back to exactly what you saved.
- **File format upgrades.** Scenes gained *layers* and *tags* (which the spec asks for), so the
  scene file format went from version 1 to version 2. Old scenes upgrade themselves when they're
  opened, and the first save keeps the old file as a backup. `noor_cli migrate` can upgrade a whole
  project at once, with a dry run first.
- **Hot reload.** Leave NoorEngine showing a building, regenerate it with BadBuilding, and the
  engine swaps in the new version by itself. It waits until the file stops changing, so it never
  grabs a half-written building.

## Build of the Week 🏆

The undo test that doesn't trust us: it takes the panel block from devlog #007 and throws **40 random
edits** at it, moves, renames, deletes, new objects, re-parenting, in **25 different random orders**.
Then it presses undo until it can't anymore and compares the scene file with the original,
**byte for byte**. Then redo all the way, and compare again. 1,000 random edits, zero differences.

## Sage vs. Reality

The file watcher's first test failed: after a change, it waited one check longer than intended
before saying "this file changed". Harmless, but a test that disagrees with the code means one of
them is wrong, and this time it was the code's counting. Now the very first check that sees a
change counts towards "it has stopped changing".

Also, a lesson for anyone writing JSON: a seed from BadBuilding last week was too big for a normal
64-bit number. The shared asset format (Noor Interchange v0.3) now says so out loud, together with
how to make generators produce *identical* results on every computer: a trick learned from
Thornwake's world generator.

## What Could Possibly Go Wrong
Undo is only as good as the commands everyone uses. The rule from now on: nothing edits a scene
behind the history's back.

## Known limitations
- Prefabs (reusable objects with overrides) are still to come.
- Textures, LOD files and moving an asset's side files (recipe, manifest) together are still to come.
- The editor that will use all this doesn't exist yet. This is the engine's half of the job.

## Next engineering target
Prefabs, then the Milestone 2 review.

## Bad Ideas Works Award for Questionable Decisions
**Winner:** Randomly vandalising a Warsaw apartment block 1,000 times to test the Undo button.
**Defence:** Every single vandalism was undone perfectly. The residents never knew.

---
*Bad Ideas Works — Code shall remain understandable to whichever AI inherits this mess.*
