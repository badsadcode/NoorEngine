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
| 2 ✅ | **A 2D character controller**: walking, jumping with variable height, slopes, one-way platforms, ground snapping; coyote time and jump buffering as options; `on_ground` events for scripts | A scripted run over a test course (slope, gap, one-way platform) lands where the maths says; a built game identical |
| 2b ✅ | **Your own controllers** (added 2026-11-20, Tom: "will the engine let me make my own controllers?"): a character whose velocity the script owns (`"motion": "script"`, Godot's CharacterBody), floor, wall and ceiling contacts, `set_velocity` on walking characters for one step, shape casts and overlaps, for Lua and C#; [Coming from Godot](../scripting/COMING_FROM_GODOT.md) | A script-owned character hangs where it's put, stops under a roof and on a floor with its velocity slid away; a double jump from a script; shape casts and overlaps by hand; a Godot-style platformer written in Lua lands, jumps and stops at a wall |
| 3 ✅ | **Sprites for 2D**: sorting layers and order in layer, sorting by Y (top-down, isometric), soft-edged (alpha) sprites, unlit sprites; frames sampled inside their own rectangle (part 2 found them bleeding) | Goldens: overlapping sprites ordered by layer, not distance; a character walking behind and in front of a tree by its Y; 3D scenes' goldens unchanged |
| 4 ✅ | **Tilemaps**: a tileset asset (from a sprite sheet: which tiles are solid, one-way, animated), a `tilemap` component (square, isometric, hexagonal), drawn in batches, colliders merged from solid tiles | A 256 × 256 map draws in one batch per chunk (cost measured); its colliders are rectangles merged along rows; goldens of each layout |
| 5 ✅ | **The editor in 2D**: a 2D view (XY, orthographic, pan and zoom), snapping to pixels or tiles, tile painting with undo, sprites dragged in from the Content Browser | API tests (paint, erase, undo; snapping); the smoke test paints a tile and sees it |
| 6 | **Resolution, display and cameras for 2D** (widened 2026-11-21: Tom asked whether a game can run full screen at the screen's own resolution yet look like a lower one, as Godot's stretch modes do): project display settings (window size, fullscreen, borderless); a **base resolution** with stretch modes like Godot's (`viewport`: the game drawn at e.g. 320 × 180 and scaled up with whole-pixel steps and nearest filtering, black bars where the shape differs; `canvas`: drawn at the screen's resolution but laid out for the base one; off) and aspect handling (keep, expand); a **render scale** for 3D (draw at 50-100% and scale up, for speed); pixel-perfect cameras (whole pixels at a chosen pixels per unit), following with a dead zone and bounds, parallax layers | A 320 × 180 game full screen at 1920 × 1080 is exactly 6 screen pixels per game pixel (readback); a moving pixel-perfect camera never draws a sprite between pixels; parallax layers move by their factor (golden at two moments); a 3D scene at 50% scale costs less GPU (measured) |
| 7 | **Samples, templates and review**: a small platformer (our own pixel art) and an isometric scene (Diablo-1-style: tiles, Y-sorted sprites, an orthographic camera at the isometric angle); both join the yardsticks. The first **project templates** (spec 24.1): Empty 2D, 2D Platformer, Top-Down 2D, each a small working start made only of public features; the review | The gate |

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

## Part 2 result (2026-11-20)
Done. The `character` component gains a 2D mode and a feel, all off by default (3D characters move exactly as before,
tested): `"plane": "xy"`, `acceleration` / `deceleration` / `air_acceleration`, `gravity_scale`, `coyote_time`,
`jump_buffer`, `jump_cut`. One-way platforms (`"one_way": true` on a static collider). `character_jump_release` and
`character_drop` for Lua and C# (rt API v10). And a built-in **`platformer`** component: keys walk and jump it, it faces
its sprite and plays idle, run, jump and fall, and a script can steer it instead. Its jump speed is solved for 60 Hz
steps, so `jump_height` is what you get.

Proved (keys pressed in the unit tests as a player would): a walk up a 20° slope and off its end without leaving the
plane on any of 150 steps; a held jump peaking at **1.50091 m** for 1.5 asked (the textbook speed would be 4% short); a
tap jumping about half as high; 6 m/s reached at 30 m/s²; a jump 0.05 s after leaving a ledge works and 0.2 s doesn't;
a jump pressed 0.05 s before landing happens and 0.25 s doesn't; up through a one-way ledge, standing on it at its top,
down + jump back through, and the same jump stopped by a solid ledge. Tom's own pixel art ran on it in a local demo
(the devlog's pictures; the art stays out of the repository).

**Not done here, said plainly:** the built-game check moves to the samples (part 7), where there's a real course to
play; ground events for scripts (`is_grounded()` is there) wait for a game that needs them; ladders (the old game had
them) are a later part. **Found:** pixel-art sprites sample with `repeat`, so a frame's edge picks up the opposite edge
or the next frame (a thin line under the hero's feet in a jump frame). Part 3 fixes it (frames sampled inside their
own rectangle).

## Part 2b result (2026-11-20)
Done, after Tom asked whether he could write his own controllers. The answer was "mostly": scripts could only ask a
character to walk or jump, so a double jump, a wall jump, a dash or a fully custom controller weren't possible, and no
script could sweep a shape through the world. Now: `"motion": "script"` gives a character's velocity to its script
(no gravity added; each step it slides and its velocity is what's left: Godot's `move_and_slide`); `is_on_floor`,
`is_on_wall`, `is_on_ceiling`, `floor_normal`; `set_velocity` on a walking character replaces its velocity for one step;
`noor.shape_cast` and `noor.overlap` (Lua; C# `Noor.ShapeCast`, `Noor.Overlap`, `IsOnFloor`..., rt API v11). And a
guide for Godot users ([COMING_FROM_GODOT.md](../scripting/COMING_FROM_GODOT.md)) mapping what Tom's old game used to
NoorEngine, with what's missing and when it's planned.

Proved: a script-owned character hangs in the air untouched, rises to a roof and stops under it (`is_on_ceiling`, upward
speed gone), drops to the floor and stops (`is_on_floor`, normal straight up); a double jump by `set_velocity` takes a
1.50 m jump to 2.61 m; a circle swept at a box hits at 4.0 m with the face's normal, overlaps find exactly what's
there; and a Godot-style platformer written in Lua lands, jumps 1.78 m, sees the wall ahead with a shape cast, and stops
against it with its run speed slid away.

## Part 3 result (2026-11-20)
Done. Sprites gain `layer`, `order`, `"sort": "y"`, `blend` and `unlit`. A sprite with any of the first four is drawn
in the see-through pass, which now sorts by layer, then order, then distance (along the view for an orthographic camera,
so sprites at one depth keep their scene order instead of a random one); before, that pass sorted by distance alone,
and every existing see-through thing is layer 0, order 0, so nothing else moved. Three material flags behind it
(`render::MaterialDesc`): **unlit** (its own colours, no light or fog), **coverage** (alpha scales everything a
see-through surface shows: the glass path still added full-strength reflections where a sprite was clear, which would
have drawn a faint rectangle round every soft sprite), and **clamp UV** (a frame sampled half a texel inside itself:
the edge bleeding part 2 found). Plain sprites are unchanged and every existing golden matched.

Proved by goldens: three walkers at one depth stacked by layer against their scene order, coins by order, soft glows
over them; a lit and an unlit coin under a dim sun; a top-down crowd listed top to bottom and drawn bottom-nearest; a
frame magnified beside a solid red one with no red pixel in the picture. And on Tom's art: the line under the hero's
feet in the jump frame is gone.

## Part 4 result (2026-11-21)
Done ([docs](../fx/TILEMAPS.md)). A `.noortileset` (a sprite sheet, its solid and one-way tiles) and a `tilemap`
component whose `rows` are one text line per row, top first (`.` empty), so a map reads like itself and diffs by row.
Square, isometric (2:1 diamonds, tall tiles rising above them, drawn back to front) and hexagonal (pointy tops) layouts.
Drawn as one mesh per 32 × 32 chunk, rebuilt only when the rows (or the tileset) change; parsed grids are shared by
drawing and collision through a cache keyed by the rows' fingerprint. Square maps collide: solid and one-way tiles
merged into as few boxes as cover them exactly, rebuilt at the next physics step when the map changes.
`e:tile` / `e:set_tile` (Lua), `Tile` / `SetTile` (C#, rt API v12).

Proved: the rows format (parse, write back, errors naming row and column, setting one cell rewrites one row); merging
(a block is one box, an L two, a checkerboard can't merge, every solid cell covered exactly once); where cells sit in
each layout; a character standing on merged tile boxes at the floor's exact top and on a one-way tile, then falling when
a script breaks the tile under it (unit); goldens of all three layouts; a 256 × 256 map (about 46,000 tiles) in 67 draw
calls with about 0.6 ms of CPU a frame (`render.tilemap_batches`).

**Found and fixed on the way:** (1) the scene gather skipped any entity it didn't already know a component of, so the
first tilemaps simply weren't there; (2) with 4× MSAA, a pixel on a tile's edge is shaded at its centre, which can lie
outside the tile, so its texture coordinate ran into the next frame of the sheet: faint lines over the isometric map.
Texture coordinates are now interpolated at the **centroid** (inside the covered part of the pixel), for every mesh.
That moved leaf edges in the Forest golden (1.4% of its pixels, indistinguishable by eye) and nothing else; its reference
was updated. **Not yet:** painting (part 5), animated tiles, auto-tiling, collision for isometric and hexagonal maps.

## Part 5 result (2026-11-21)
Done. **The 2D view**: a toggle in the viewport's toolbar; the editor camera looks straight down -Z at the XY plane,
orthographic; right or middle drag (or Alt + left) pans exactly with the pointer, the wheel zooms keeping the point
under the pointer where it is, F frames (tilemaps included). **Painting tiles**: the Tilemaps panel (a tileset picker
and New tilemap; a palette cut from the tileset's picture, crisp; the eraser; Paint; Snap to tiles and to pixels); the
viewport outlines the cell under the pointer (squares, diamonds, hexagons) and turns left-drags into "tiles" events,
one per cell reached; the document paints them (`noor_doc_tilemap`, editor API v30), a stroke one undo step, growing
the map past its edges and moving the entity when it grows left or down so nothing already painted moves.

Proved: the cell under a point is the inverse of where cells sit, in all three layouts (49 cells each, and points
inside them); painting grows maps, erasing never does, hexagonal maps grow downwards by two rows to keep odd rows odd
(unit); the 2D camera pans with the pointer, zooms at it and frames an XY box (unit); the panel's commands: listing,
the palette's frames, a stroke as one undo step, growing left and down with the entity moved back, refusing a
non-tilemap (API); and in the running editor, a drag across a map in the 2D view paints the four cells it crosses as
one undo step, and one undo takes it back (smoke test).

**Not done here:** dragging an entity itself in the 2D view (the gizmo's X and Y arrows move it; there's no
drag-the-sprite yet); a 2D grid drawn in the view; rectangle and fill tools; an eyedropper. Sprites from the Content
Browser can be added to the scene as before (Add to scene).

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
