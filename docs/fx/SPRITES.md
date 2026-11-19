# Sprites

Milestone 8B, part 5 ([ADR-035](../decisions/ADR-035-particles-and-sprites.md)). A sprite is a picture standing in the
world: a character, a creature, a coin, a sign. It's cut from a **sprite sheet** (a `.noorsprite` file pointing at a
texture) and drawn on a flat card that turns to face the camera. Sprites are lit by the sun and the sky, cast and
receive shadows, hide behind things and fade into fog, like any other surface.

## The sprite sheet (`.noorsprite`)

```json
{
  "noor_sprite": 1,
  "name": "Walker",
  "texture": "<the texture's asset id>",
  "grid": {"columns": 4, "rows": 2},
  "margin": 0, "spacing": 0,
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

Tip: for pixel art, set the texture's **Compression** to None (texture settings) to keep every colour exact. The
default (BC7) is very close, but not identical.

## The `sprite` component

```json
"sprite": {"sheet": "<.noorsprite id>", "animation": "walk", "speed": 1, "time_offset": 0,
           "frame": 0, "billboard": "upright", "flip_x": false, "color": "#FFFFFF", "cast_shadows": true}
```

| Field | What it does |
|---|---|
| `sheet` | The sprite sheet. |
| `animation` | The animation to play. Without one, `frame` is shown still. |
| `speed`, `time_offset` | Play faster or slower; start part-way in (so a crowd doesn't walk in step). |
| `billboard` | `upright` turns round the vertical to face the camera (characters, trees: they stay standing). `camera` faces it fully (smoke puffs, coins). `none` keeps the entity's own rotation (a sign on a wall). |
| `flip_x` | Mirror left to right (a character walking the other way). |
| `color` | Multiplies the picture (sRGB `#RRGGBB`, or linear `[r, g, b]`). |
| `cast_shadows` | Whether it throws a shadow. |

The entity's position is where the pivot goes; its scale multiplies the frame's size.

## In the editor

- **Make a sheet:** open a texture's settings (Content Browser, Edit) and press **Make sprite sheet**. The sheet is
  saved beside the texture and opens in its own tab.
- **The sheet editor** shows the picture with every frame outlined and numbered. Set the grid, the size in the world,
  the pivot and the filter on the right; add animations and type their frames (`8-11`, or `0, 1, 1, 2`). The preview
  under the sheet plays the selected animation. Everything saves as you edit, and sprites in the viewport follow.
- **Place in scene** adds a sprite entity playing the previewed animation (or switches the selected sprite to this
  sheet). Content Browser's **Add to scene** does the same.
- In the viewport, sprites turn to the editor's camera, animate, can be clicked to select and framed with F.

## Not yet

- 8-direction characters (a row per direction, picked from the camera angle) and playing animations from scripts:
  part 6.
- Clicking selects a sprite anywhere on its card, transparent corners included.
- Animations in the editor's viewport (Play mode too) run on the viewport's own clock; in games, on the game's time.
