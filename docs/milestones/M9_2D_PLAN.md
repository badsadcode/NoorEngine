# 2D, 2.5D and isometric foundations: plan

**Where it sits:** between 9A part 1 and part 2 ([why](M9_PLAN.md#the-order-from-here-sage-2026-11-20)). Tom asked
(2026-11-20) for 2D, 2.5D and isometric games to be possible as well as 3D. **Decisions:**
[ADR-040](../decisions/ADR-040-2d-and-isometric.md).

**Goal:** a 2D platformer, a top-down game and a Diablo-1-style isometric game can each be made in NoorEngine with its
own tools, without a second engine inside the engine, and without costing 3D anything.

**Gate:** each piece has editor authoring, runtime behaviour, serialization, docs, performance notes and tests (the
Milestone 8 gate, kept); a small platformer and an isometric scene are samples and yardsticks; built games play them to
the bit; every existing golden and yardstick unchanged.

## Where it starts

- **Have (9A part 1 and before):** orthographic cameras; sprites from `.noorsprite` sheets with animation, 8 directions
  and nearest filtering (8B), drawn as lit cut-out cards sorted by depth; Lua and C#; the HUD; Jolt physics in 3D with a
  capsule character controller; particles; deterministic fixed steps and built-game checks; NoorPong.
- **Haven't:** physics that stays in a plane; 2D collider shapes; a platformer character controller; sprites ordered by
  layer rather than distance; soft-edged or unlit sprites; tilemaps; an editor view for 2D; pixel-perfect cameras;
  parallax.

## Parts (each ends tested, documented and pushed)

| Part | What | Proves |
|---|---|---|
| 1 ✅ | **2D physics on Jolt**: a rigidbody can be locked to a plane (`"plane": "xy"`: moving in X and Y, turning about Z) or have any axis locked (`lock_position`, `lock_rotation`, for 3D too); 2D collider shapes: `rectangle`, `circle`, `polygon` (convex, from points), each a slab of a given depth | Bodies locked to the plane stay in it to the bit over a long tumbling pile, turning only about Z (unit); a circle rolls down a slope as a disc should (its speed against the formula); polygons land and rest; deterministic run to run; a golden of a 2D pile; existing physics tests unchanged |
| 2 | **A 2D character controller**: walking, jumping with variable height, slopes, one-way platforms, ground snapping; coyote time and jump buffering as options; `on_ground` events for scripts | A scripted run over a test course (slope, gap, one-way platform) lands where the maths says; a built game identical |
| 3 | **Sprites for 2D**: sorting layers and order in layer, sorting by Y (top-down, isometric), soft-edged (alpha) sprites, unlit sprites | Goldens: overlapping sprites ordered by layer, not distance; a character walking behind and in front of a tree by its Y; 3D scenes' goldens unchanged |
| 4 | **Tilemaps**: a tileset asset (from a sprite sheet: which tiles are solid, one-way, animated), a `tilemap` component (square, isometric, hexagonal), drawn in batches, colliders merged from solid tiles | A 256 × 256 map draws in one batch per chunk (cost measured); its colliders are rectangles merged along rows; goldens of each layout |
| 5 | **The editor in 2D**: a 2D view (XY, orthographic, pan and zoom), snapping to pixels or tiles, tile painting with undo, sprites dragged in from the Content Browser | API tests (paint, erase, undo; snapping); the smoke test paints a tile and sees it |
| 6 | **Cameras for 2D**: pixel-perfect (whole pixels at a chosen pixels per unit, integer scaling for retro resolutions), following with a dead zone and bounds, parallax layers | A moving pixel-perfect camera never draws a sprite between pixels (readback); parallax layers move by their factor (golden at two moments) |
| 7 | **Samples and review**: a small platformer (our own pixel art) and an isometric scene (Diablo-1-style: tiles, Y-sorted sprites, an orthographic camera at the isometric angle); both join the yardsticks; the review | The gate |

## Part 1 result (2026-11-20)
Done. `"plane": "xy"` on a rigidbody (Jolt's `EAllowedDOFs::Plane2D`), `lock_position` / `lock_rotation` for any axes,
`angular_damping`; 2D collider shapes `rectangle`, `circle` (a sphere given a disc's inertia) and `polygon` (a convex
hull of the points extruded by `depth`). Proved: a tumbling pile of 18 rectangles, circles and triangles stays in its
planes **to the bit** for eight seconds and turns only about Z, while the same pile unlocked drifts out (the control);
the same pile twice lands identically; a circle rolls down a 20° slope at **2.23677 m/s²** against the disc formula's
2.23681 (a ball would do 2.397: why circles get a disc's inertia); a triangle lands and rests on a side; a 3D post with
X and Z turning locked stands on a step's edge it would otherwise topple off; a polygon of two points is refused. A
golden of a 2D pile under an orthographic camera (`Physics2D_Pile`). Risk 1 (Jolt in a plane) is settled for bodies;
characters are part 2.

## Highest risks (settle early)

1. **Jolt in a plane.** Locked degrees of freedom are Jolt's own feature (`EAllowedDOFs`), but contacts between slabs,
   rolling and stacking must look like 2D physics people expect from Box2D. Part 1 measures the classic cases before
   anything builds on it; if Jolt can't, a 2D engine beside it is the fallback (and a new ADR).
2. **Sorting.** Lit, depth-tested cut-outs and soft-edged sprites ordered by layer are different pipelines. Part 3 keeps
   3D's path untouched (goldens) and adds the 2D order beside it.
3. **Pixel-perfect and the rest of the frame.** MSAA, tonemapping and HDR blur and shift pixel art. Part 6 decides what a
   pixel-art game turns off (and ties in with the yardsticks' finding: small games shouldn't carry render targets they
   don't use).
4. **The editor.** The viewport is built around a 3D orbit camera. The 2D view has to feel native, not like a locked 3D
   camera.

## Not here (later)

2D lighting (normal-mapped sprites, 2D lights and shadows), skeletal 2D animation, auto-tiling rules, 2D particles beyond
what 8B's do in a plane, 2D navigation (comes with the gameplay foundations' navigation).
