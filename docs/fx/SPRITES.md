# Sprites

Milestone 8B, parts 5 and 6 ([ADR-035](../decisions/ADR-035-particles-and-sprites.md)). A sprite is a picture standing in the
world: a character, a creature, a coin, a sign. It's cut from a **sprite sheet** (a `.noorsprite` file pointing at a
texture) and drawn on a flat card that turns to face the camera. Sprites are lit by the sun and the sky, cast and
receive shadows, hide behind things and fade into fog, like any other surface.

## The sprite sheet (`.noorsprite`)

```json
{
  "noor_sprite": 1,
  "name": "Walker",
  "texture": "<the texture's asset id>",
  "grid": {"columns": 4, "rows": 2, "margin": 0, "spacing": 0},
  "pivot": [0.5, 0.03125],
  "pixels_per_metre": 16,
  "filter": "nearest",
  "animations": {
    "walk": {"frames": {"from": 0, "to": 3}, "fps": 6, "loop": true},
    "die":  {"frames": [28, 29, 30, 31], "fps": 4, "loop": false}
  }
}
```

- **Grid:** either `columns` and `rows` (the frame size follows), or `frame_width` and `frame_height` in pixels (as
  many frames as fit). `margin` is a border round the whole sheet, `spacing` the gap between frames. A grid that
  doesn't fit the picture is refused, not guessed.
- **Frames** count from 0, left to right, then top to bottom.
- **Pivot:** the point of the frame the entity stands on, from its bottom-left corner: `[0.5, 0]` is the bottom middle
  (the feet, the default), `[0.5, 0.5]` the centre.
- **Pixels per metre:** how big a frame is in the world. 32-pixel frames at 16 are 2 m tall.
- **Filter:** `nearest` (the default) keeps pixel art crisp; `linear` smooths painted sprites.
- **Animations:** frames as a range (`from`/`to`, backwards if `to` is smaller) or a list (repeats allowed), a speed in
  frames a second, looping or holding the last frame.

## 8-direction characters

A character seen from all round has a view per direction. Like The Uncounted's sheets (Coth's is in the test data),
NoorEngine puts the views in the **columns**: add `"directions": 8` and column 0 faces the viewer, then each column
turns a step further to the viewer's left (2 faces left, 4 away, 6 right). Rows are animations.

```json
"directions": 8,
"animations": {
  "idle":   {"frames": [0], "fps": 2},
  "walk":   {"frames": [8, 16], "fps": 6},
  "attack": {"frames": [32], "fps": 6, "loop": false},
  "die":    {"frames": [40, 44], "fps": 3, "loop": false, "directional": false}
}
```

- An animation names its frames in the **first column** (direction 0); facing another way, the same row is read that
  many columns along.
- Which view shows comes from the angle between where the entity faces (its −Z, like every NoorEngine entity) and the
  camera, rounded to the nearest view.
- **In a flat game** (2D, top-down, isometric: the XY plane seen straight on) there is no "turning to the camera". The
  script says which way the character faces on screen instead: `"facing": [x, y]` on the sprite (down `[0, -1]` is
  view 0, left view 2, up 4, right 6). `NoorIsometric` and the Top-Down 2D template set it from the walking direction:

  ```lua
  local sprite = e:get("sprite")
  sprite.facing = { dir.x, dir.y }
  e:set("sprite", sprite)
  ```
- Sheets with 4 views work the same way (down, left, up, right).
- `"directional": false` shows the frames as they are from every side (lying down, a puff of dust).
- The columns must be a multiple of the directions, or the sheet is refused.

Tip: for pixel art, set the texture's **Compression** to None (texture settings) to keep every colour exact. The
default (BC7) is very close, but not identical.

## The `sprite` component

```json
"sprite": {"sheet": "<.noorsprite id>", "animation": "walk", "speed": 1, "time_offset": 0, "started": 12.5,
           "frame": 0, "billboard": "upright", "flip_x": false, "color": "#FFFFFF", "cast_shadows": true}
```

| Field | What it does |
|---|---|
| `sheet` | The sprite sheet. |
| `animation` | The animation to play. Without one, `frame` is shown still. |
| `speed`, `time_offset` | Play faster or slower; start part-way in (so a crowd doesn't walk in step). |
| `started`, `paused_at` | Written by scripts (below): when the animation they played started (game seconds), and where it froze. |
| `billboard` | `upright` turns round the vertical to face the camera (characters, trees: they stay standing). `camera` faces it fully (smoke puffs, coins). `none` keeps the entity's own rotation (a sign on a wall). |
| `flip_x` | Mirror left to right (a character walking the other way). |
| `color` | Multiplies the picture (sRGB `#RRGGBB`, or linear `[r, g, b]`). |
| `cast_shadows` | Whether it throws a shadow. |
| `layer`, `order` | **2D drawing order** (2D foundations, part 3): lower layers are drawn first (behind); within a layer, lower `order` first. Godot's `z_index`. |
| `sort` | `"y"`: within its layer, the lower its pivot, the nearer it is: top-down and isometric scenes, where a character walks behind a tree and then in front of it. Godot's `y_sort_enabled`. |
| `blend` | `true`: soft edges (alpha blending), for glows, smoke, anti-aliased art. |
| `unlit` | `true`: shows its own colours exactly, whatever the light (no sun, no shadow, no fog). Godot's unshaded material. |
| `facing` | `[x, y]`: in a flat game, which way it faces on screen; picks the view of a directional sheet (above). |
| `y_depth` | `true`: an **isometric** character among wall tiles: it's given the map's depth rule (by its pivot's height), so a pillar in front hides it and it hides a pillar behind it, pixel by pixel (below). |

The entity's position is where the pivot goes; its scale multiplies the frame's size.

### Drawing order in 2D

In 3D, what's in front is decided by depth: nearer things hide farther ones. In 2D every sprite usually sits at the
same depth, so the artist decides instead. A sprite with `layer`, `order`, `sort` or `blend` is drawn in the
see-through pass in that order: by layer, then order (or height, with `"sort": "y"`), then back to front. One without
any of them is a lit cut-out, depth-tested like any 3D surface: right for sprites standing in a 3D world (The
Uncounted's style), and every scene made before this works as it did.

**Isometric walls and characters** (2D part 7). Sorting can't put a character *between* two tiles of a tilemap: a
chunk of tiles is one draw. So isometric and hexagonal maps sort by depth instead: each tile sits a hair further back
the higher it is on screen (a thousandth of a metre per metre), and a sprite with `"y_depth": true` (and no `layer`,
`order`, `sort` or `blend`) follows the same rule from its pivot. Depth testing then sorts every pixel: Coth's legs
disappear behind a pillar's base while his head shows above it. Hard-edged sprites only: soft-edged ones don't write
depth.

A sprite only ever shows its own frame: its edges are sampled half a texel inside the frame, so the frame next to it
in the sheet (or the opposite edge) can't bleed in, even magnified.

## From scripts

Lua (`e:play_animation("attack")`) and C# (`Entity.PlayAnimation("attack")`) play an animation from its start, and
a non-looping one tells the script when it has finished: `on_animation_end(name)` / `OnAnimationEnd(name)`. Then
`stop_animation` / `StopAnimation` freezes it, `set_animation_speed` / `SetAnimationSpeed` plays on faster or slower
without a jump, and `animation()` / `Animation` says what it's playing ([Lua](../scripting/LUA_API.md),
[C#](../scripting/CSHARP_API.md)). It all lives in the component (`started`, `speed`, `paused_at`), so a saved game or
the editor sees the same thing.

```lua
function Coth:on_update(dt)
    if noor.input.pressed("attack") then self.entity:play_animation("attack") end
end
function Coth:on_animation_end(name)
    if name == "attack" then self.entity:play_animation("idle") end
end
```

## In the editor

- **Make a sheet:** open a texture's settings (Content Browser, Edit) and press **Make sprite sheet**. The sheet is
  saved beside the texture and opens in its own tab.
- **The sheet editor** shows the picture with every frame outlined and numbered. Set the grid, the size in the world,
  the pivot, the filter and the directions on the right; add animations and type their frames (`8-11`, or
  `0, 1, 1, 2`). The preview under the sheet plays the selected animation; on a directional sheet, ⟲ and ⟳ turn it.
  Everything saves as you edit, and sprites in the viewport follow.
- **Place in scene** adds a sprite entity playing the previewed animation (or switches the selected sprite to this
  sheet). Content Browser's **Add to scene** does the same.
- In the viewport, sprites turn to the editor's camera, animate, can be clicked to select and framed with F.

## Not yet

- Clicking selects a sprite anywhere on its card, transparent corners included.
- Sheets with fewer drawn views that mirror the rest (5 directions plus flips) aren't supported: every view is drawn.
- Animation events in the middle of an animation (a footstep on frame 2): only the end is reported so far.
