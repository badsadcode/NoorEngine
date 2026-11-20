# ADR-040: 2D, 2.5D and isometric games in a 3D engine

**Status:** Accepted for part 1 (physics); the rest proposed, settled part by part
**Date:** 2026-11-20

## Context
Tom wants 2D platformers, top-down and isometric games (Diablo-1-style) as well as 3D ones (M9 plan, principle 0;
[M9_2D_PLAN.md](../milestones/M9_2D_PLAN.md)). NoorEngine is a 3D engine: positions, physics (Jolt), rendering and the
editor are all 3D. Engines answer this in two ways: a separate 2D world beside the 3D one (its own physics engine,
renderer and editor; Godot, Unity's 2D physics), or 2D as a constrained use of the 3D one.

## Options
- **A: A separate 2D stack** (Box2D, a 2D renderer, 2D nodes). Physics that is exactly 2D; but two physics engines to
  keep deterministic, two sets of components, scenes that can't mix 2D and 3D, and twice the tooling.
- **B: 2D inside the 3D engine.** The XY plane is the 2D world; sprites and tiles are flat things in it; physics is Jolt
  with bodies locked to the plane; the camera is orthographic. One engine, one set of tools, 2D and 3D mixable (2.5D for
  free); some care to make 2D physics feel right.

## Decision
**B.** The 2D world is the **XY plane, +Y up, +X right**, the camera looking down −Z (Create ▸ Camera (2D)); metres, so
pixel art says how many pixels make a metre (`pixels_per_metre`, as sprites already do).

- **Physics (part 1):** Jolt with allowed degrees of freedom. `"plane": "xy"` on a rigidbody locks it to moving in X
  and Y and turning about Z (`EAllowedDOFs::Plane2D`); `lock_position` / `lock_rotation` lock any axes (useful in 3D
  too: a body that must stay upright). 2D shapes are thin slabs: `rectangle` (`size` [w, h]), `circle` (`radius`: a
  sphere, which touches a plane exactly as a disc does, given a disc's inertia so it rolls like one), `polygon` (convex `points` [[x, y], ...], extruded); `depth`
  (1 m by default) is how thick the slab is, so 2D bodies in the same plane always meet. Static bodies need no lock.
- **Sprites, tiles, cameras and the editor** (parts 3–6): decided in those parts, here.

## Consequences
- One physics engine: determinism and built-game checks work for 2D as they do for 3D. If Jolt can't make 2D physics
  feel right in part 1's cases, a 2D physics engine is the fallback and gets its own ADR.
- 2D and 3D mix in one scene: 3D models in a 2D game (2.5D), sprites in 3D worlds (The Uncounted).
- Nothing 3D changes: the lock is opt-in per body, the shapes are new names.

## Reviewed by
Sage.
