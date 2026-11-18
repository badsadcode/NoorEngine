# NoorEngine Devlog #043 — An Oak Walks In

**Date:** 2026-11-18
**Build / commit:** Milestone 7, part 5

## This week we actually did

**A tree from NoorMD stands in the editor, and the engine knows its whole story.**

![The editor with a textured oak on the test floor beside a crate and the player capsule. Below, the Content Browser's new Details pane: Oak_01, Model · vegetation.tree, made by NoorForm, recipe Oak_01.recipe.json seed 1, family Oak variant A of 9, and four levels of detail with their triangles, sizes and distances](img/devlog043_oak.png)

*A real screenshot from the editor's smoke test. Oak 01, made by NoorMD's tree bench NoorForm,
standing next to a 1 m crate and the player. Below it, the Content Browser's new Details pane.*

- **One tree, one asset.** An exported tree is six files: the model, three lower levels of detail,
  a manifest and a recipe. The engine treats the set as one asset, with the id from the manifest,
  so scenes keep pointing at the tree when it's regenerated.
- **Every level is checked.** Each level's file is opened and compared with what the manifest
  promises: triangles, which level it is, the distance it's for, the billboard flag. Any mismatch,
  missing file or changed file shows up as a problem, in the editor and in `noor_cli`.
- **The story is visible.** Select the tree in the Content Browser and the Details pane tells you who
  made it, from which recipe and seed, which family it belongs to (variant A of nine oaks), and what
  each level costs.
- **The trunk is solid.** The tree comes with a collision capsule around its trunk. Placing it in a
  scene turns that into a collider of the right size: 0.68 m. It used to quietly become 0.3 m.

## Build of the Week 🏆

**The id the guide predicted.** Last week's guide for NoorMD said that "noorform:tree:Oak 01:1" turned
into a name-based id would give `719beb3e-…`. This week a script did exactly that, and the engine
read back exactly that id. A number written in a document and later confirmed by running code is
my favourite kind of number.

## Sage vs. Reality

**NoorMD doesn't write the new layout yet.** So there was nothing real to import. Rather than teach
the engine NoorMD's old pack format (the thing we promised not to do), I wrote a small converter
that turns one of the studio's NoorMD oaks into the new layout, following our own guide rule by rule. It lives
outside the engine. When NoorMD gets its NoorEngine export mode, the real export replaces the
converted oak.

**The guide's numbers were optimistic.** I guessed an oak would shrink from 19 MB to about 4 MB. It's
6.5 MB. The textures shrank as planned. Now the geometry is half the file, and the guide says how
NoorMD could halve that too.

**The trunk isn't where the pivot is.** Fitting the collision capsule showed that Oak 01's trunk stands
about a metre from the point that's supposed to be its base. That's written up for NoorMD as a finding.
I didn't quietly fix it in the converter.

**The first screenshot had no tree in it.** Two reasons. In a Debug build, loading the oak and
compressing its textures takes a few seconds, and the test took its picture before the tree arrived.
Also, the tree was planted inside the BadBuilding block, which already stands at the origin. The test
now waits until the viewport is really drawing the oak's 15,268 triangles, and plants it on open
ground first.

## Next engineering target
**Part 6: forests that don't melt the GPU.** Draw many trees in a few draw calls (instancing), and pick
each tree's level of detail by its distance from the camera, using the distances the manifest
already carries. The goal: 2,000 oaks at 60 FPS, with the numbers written down.

## Bad Ideas Works Award for Questionable Decisions
**Winner:** A Details pane that reports a tree's licence, family tree and repeatability, but can't make
it sway in the wind.
**Defence:** The wind data is kept, and the pane says honestly that nothing uses it yet. Swaying is a
future milestone's problem. The future milestone has been notified.
