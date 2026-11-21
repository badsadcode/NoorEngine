# 2D, 2.5D and isometric foundations: gate review

**Date:** 2026-11-21. **Plan:** [M9_2D_PLAN.md](M9_2D_PLAN.md). **Decisions:** [ADR-040](../decisions/ADR-040-2d-and-isometric.md).

**The goal was:** a 2D platformer, a top-down game and a Diablo-1-style isometric game can each be made in NoorEngine
with its own tools, without a second engine inside the engine, and without costing 3D anything. **Verdict: met**, with
the exceptions below. 2D is the XY plane of the 3D world: one physics engine (Jolt, locked to the plane), one renderer,
one editor. The platformer and the isometric game exist as samples (`NoorPlatformer`, `NoorIsometric`), the top-down
game as a template, and all three play identically in built games.

## Deliverables (the plan's parts)

| Part | What | Status | Evidence |
|---|---|---|---|
| 1 | 2D physics on Jolt: bodies locked to the XY plane, any axis locked, `rectangle`, `circle`, `polygon` | ✅ | In the plane to the bit over a long pile; a circle rolls within 0.002% of the formula; golden `Physics2D_Pile` |
| 2 | A 2D character with platformer feel; one-way platforms; the built-in `platformer` | ✅ | A held jump peaks within 0.06% of the height asked; a scripted course; a built game identical |
| 2b | Your own controllers (`"motion": "script"`, floor/wall/ceiling, shape casts, overlaps), Lua and C#; Coming from Godot | ✅ | Script-owned characters slide, stop, double-jump; a Godot-style platformer in Lua |
| 3 | Sprites for 2D: layers, order, sort by height, soft-edged and unlit; frames that never bleed | ✅ | Goldens `Sprites2D_*`; 3D goldens unchanged |
| 4 | Tilemaps: square, isometric, hexagonal; chunks; merged collision; tiles from scripts | ✅ | A 256 × 256 map in 64 draws (~0.6 ms CPU); goldens `Tilemap_*`; a character falling through a broken floor |
| 5 | The editor in 2D: the 2D view, the Tilemaps panel, painting with undo | ✅ | API tests; the smoke test paints a row in the 2D view and undoes it |
| 6 | Resolution, display and cameras: base resolution and stretch modes, render scale, window modes, pixel snap, follow, parallax | ✅ | Readback: every game pixel exactly 4 × 4, bars black, `expand`'s middle identical to `keep`; golden `Camera2D_Follow`; Hillside at half scale measured |
| 7 | Samples, templates and review | ✅ | This document; below |

## Gate criteria, system by system

| System | Editor authoring | Runtime | Serialization | Docs | Performance notes | Tests |
|---|---|---|---|---|---|---|
| 2D physics | Inspector (rigidbody `plane`, 2D collider shapes) | Jolt `Plane2D`, slabs | components | LUA_API.md, ADR-040 | as 3D bodies | unit, golden |
| 2D characters, controllers | Inspector (`character`, `platformer`) | Jolt CharacterVirtual in the plane | components | LUA_API.md, CSHARP_API.md, COMING_FROM_GODOT.md | as 3D characters | unit, built games |
| Sprites for 2D | Inspector; the sprite editor | the see-through pass by layer/order/height; `facing`, `y_depth` (part 7) | `sprite`, `.noorsprite` | SPRITES.md | as 3D sprites (part 3) | unit, goldens |
| Tilemaps | the Tilemaps panel, painting in the 2D view | chunks of 32 × 32, merged boxes, depth by height for isometric (`floor` tiles, part 7) | `tilemap` (text rows), `.noortileset` | TILEMAPS.md | 64 draws, 0.6 ms for 256 × 256 | unit, API, goldens, smoke |
| The 2D editor view | itself | — | the viewport's `"view"` | TILEMAPS.md, PBR.md | — | unit, smoke |
| Display settings | **none yet** (the file by hand: exception 1) | `layout_display`, the scene at its own size, the tonemap pass places it | `Settings/display.json` | DISPLAY.md | a 320 × 180 game: 0.36 ms GPU, 68 MB GPU memory | unit, readback |
| Cameras (snap, follow, parallax) | Inspector (`camera`, `camera_follow`, `parallax`) | `CameraFollowSystem`, parallax in the scene gather | components | DISPLAY.md | — | unit, golden |
| Project templates | New Project (the Project Hub) | — (files) | `templates/<id>/template.json` | [TEMPLATES.md](../TEMPLATES.md) | — | unit, CLI per template, smoke |

## The samples and templates (part 7)

**NoorPlatformer** (`samples/NoorPlatformer`): a short level in Tom's own hand-drawn pixel art (used with his
permission), 320 × 180 scaled up in whole pixels: a tilemap of stone, brick and one-way planks, the built-in
`platformer` hero, eight coins, two beds of spikes, a door, a follow camera, parallax mountains and pines. One Lua
script is the whole game, with an autopilot until a key is pressed. **Proves:** the level can be finished (the
autopilot reaches the door, `sample.noorplatformer_finishes`); a built game plays it to the bit.

**NoorIsometric** (`samples/NoorIsometric`): a torch-lit dungeon room in 2:1 isometric tiles drawn by the generator, with
Coth from The Uncounted walking round four pillars in eight directions. **Proves:** walls in front hide a character and
walls behind don't, pixel by pixel; floors never hide a character's feet; point lights on sprites and tiles; a built game
to the bit.

**Templates** (`templates/`; spec 24.1): **Empty 2D**, **2D Platformer**, **Top-Down 2D**: small working starts made
only of public features, from `noor_cli new <folder> <name> --template <id>` or the editor's New Project. **Proves:**
each makes a project that plays with no errors under validation (`cli.template_*`); the editor offers them and opens a
project made from one (smoke test).

## What the samples found

1. **8-direction sprites had no meaning in a flat game.** The view came from the angle to the camera in the ground
   plane; seen straight on, that's always the same. Added `"facing": [x, y]` (the script says which way on screen).
2. **Isometric characters couldn't walk behind walls.** Sorting puts a whole chunk of tiles before or after a sprite.
   Added `"y_depth": true`: the map's depth rule for the character, so depth testing sorts every pixel.
3. **...then floors covered feet** (Tom spotted it, watching): a floor tile is placed by its centre but reaches half a
   cell higher, so the tile in front briefly counted as nearer. Added the tileset's `floor` list (a cell further back;
   Godot's Y-sort origin) and a test that walks a character over every point of a tile.
4. **My platformer level had a pit you couldn't leave and a bridge you couldn't reach** (3 m up; a jump is 2.2 m). The
   autopilot found both. Every ledge is now at most 2 m above where you jump from, and the finish is a test.
5. **Scripts saved with a byte-order mark didn't load** (Notepad and PowerShell write one). The loader skips it now.
6. **The editor's build took its tools from the Debug folder** (whatever its own configuration): the smoke test had
   been packing games with a stale player. It takes them from the DLL's own configuration now.
7. **A character can't be teleported by `set_position`** (physics owns where it is): a respawn needs a teleport. Not
   fixed here: in the ideas inbox for the gameplay foundations (spawning and levels).
8. **The 9A finding confirmed and answered:** small games paid for render targets at the window's size. With a base
   resolution they don't: NoorPlatformer's GPU memory is 68 MB against Pong's 117 MB, its GPU frame 0.36 ms against
   1.23 ms.

## Performance notes

| Yardstick | CPU/frame median (p95) | GPU/frame | Working set | GPU resident | First frame |
|---|---|---|---|---|---|
| NoorPlatformer | 0.39 ms (0.59) | 0.36 ms | 198 MB | 68 MB | 0.64 s |
| NoorIsometric | 0.23 ms (0.41) | 1.01 ms | 197 MB | 118 MB | 0.62 s |
| NoorPong (for comparison, 9A baseline) | 0.28 ms (0.37) | 1.23 ms | 197 MB | 117 MB | 0.65 s |

Both joined the yardsticks (`tests/yardsticks.json`, measured with `measure_yardsticks.py --only`, the other four
baselines untouched). The other yardsticks and every golden made before the 2D foundations are unchanged.

## Exceptions (accepted, tracked)

1. **Display settings have no editor page.** `Settings/display.json` is edited by hand (it's small and documented).
2. **Isometric and hexagonal maps don't collide.** Games ask the map (`e:tile`) as NoorIsometric does. Collision for
   them is in TILEMAPS.md's "Not yet".
3. **No teleport for characters** (finding 7).
4. **Templates are 2D only.** Empty 3D, First-Person, Third-Person and the rest of spec 24.1 come with the groups that
   make them worth having; "Custom" (saving your own project as a template) isn't here yet, though any folder with a
   `template.json` works with `noor_cli new --template <folder>`.
5. **The parallax golden is one moment, not two** (part 6; the motion is unit-tested).

## Open questions

- Should a sprite with `y_depth` also take part in the see-through ordering (soft-edged characters in isometric maps)?
  Today it must be hard-edged. Not needed by either sample.

## Next

The gameplay foundations ([M9_PLAN.md](M9_PLAN.md#the-order-from-here-sage-2026-11-20)): spawning, timers, tweens,
signals, groups, animating any property, game UI, saving, level changes, navigation. Both samples will want them
(a respawn, a pause menu, a second level).
