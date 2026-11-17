# NoorEngine Devlog #031 — Cutting Holes in Walls

**Date:** 2026-11-17
**Build / commit:** Milestone 6, part 1

## This week we actually did

**Milestone 6 is about building things inside the editor**: walls, doors, stairs, hills. Its first
part is the riskiest one, so it came first and has no buttons yet: the maths of **cutting one shape
out of another** (CSG, "constructive solid geometry").

Take a box for a wall, a box for a door, and *subtract* the door from the wall: you have a doorway.
Add a wedge for a roof and a cylinder for a tower, cut arched windows out of it, and you have a
castle. Level editors have done this since Quake. Getting it **robust** is famously hard. Shapes
that touch exactly, edges that line up perfectly, rotated pieces and tiny slivers all make weak
CSG code produce holes, cracks or crashes.

So before writing a single button, we tried to break it:

- a doorway whose bottom lines up exactly with the bottom of the wall (the classic trap);
- a wall turned 37°, with a window stretched and turned the same way;
- a window tilted 20° through the wall;
- two boxes touching face to face (no wall may be left between them);
- a box inside a box (a hollow room);
- a wall with **100 windows**.

Every case came out with the exact volume it should have, down to the fourth decimal. The 100-window
wall builds the same, byte for byte, every time, in about **14 milliseconds**. That's fast enough
that dragging a window around will update the wall as you drag.

## Bricks stay where they are

The part we care about most for real levels: a wall made of bricks, with a doorway cut into it,
should still look like the same brick wall. The bricks around the door don't move. The new
surfaces inside the doorway get the door brush's material, already lined up with the world. That's
tested too: every point on the wall's face has exactly the texture position it had before the cut.

## Build of the Week 🏆

**A test that guards the border.** Editing tools must never end up inside a shipped game: games
get the finished meshes, not the machinery that made them. A new test reads the build's own wiring
and fails if the CSG code ever becomes part of the game player. It also checks that it can see the
CSG code where it *does* belong, because a guard that can't see anything also never fails.

## Sage vs. Reality

**The semicolon, again.** A test called "touching boxes merge…; a brush inside another…" ran zero
tests: the test runner treats a semicolon as "this is a list". Third time this month. The test
names have commas now, and I've started to read semicolons as tiny traps.

## Under the hood

The boolean maths comes from **Manifold**, an open-source library (Apache-2.0) that Godot also uses
for its CSG. NoorEngine wraps it in its own small module, so the rest of the engine only knows
"brushes in, mesh out". We pinned a release that's two months old, not last week's.

## Next engineering target
**Part 2: brushes in the editor.** Create ▸ Brush, numbers in the Inspector, the hierarchy as the
order of operations, and a house that rebuilds while you drag its walls.

## Bad Ideas Works Award for Questionable Decisions
**Winner:** Testing a level editor by putting 100 windows in one wall.
**Defence:** Somebody's going to build a greenhouse.
