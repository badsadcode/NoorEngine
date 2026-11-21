# Display: resolution, stretch modes and 2D cameras

2D foundations, part 6 ([plan](../milestones/M9_2D_PLAN.md), [ADR-040](../decisions/ADR-040-2d-and-isometric.md)). How a
game's picture fills the window or the screen, and the cameras 2D games want: following, pixel-snapped, with parallax
layers behind them. All of it is optional. A project without `Settings/display.json` plays exactly as it did before (a
1280 × 720 window, the scene drawn at the window's size).

## Settings/display.json

```json
{
  "window": {"width": 1280, "height": 720, "mode": "windowed"},
  "base_resolution": [320, 180],
  "stretch": "viewport",
  "aspect": "keep",
  "integer_scale": true,
  "render_scale": 1.0
}
```

| Setting | Values | What it does |
|---|---|---|
| `window.width`, `window.height` | pixels (default 1280 × 720) | The window's size when windowed |
| `window.mode` | `windowed`, `fullscreen`, `borderless` | Fullscreen uses the desktop's own resolution (the monitor's mode is never changed); borderless is a window without a frame |
| `base_resolution` | `[width, height]` | The resolution the game is designed at. Without one, the window's own size |
| `stretch` | `viewport`, `canvas`, `off` | How the base resolution becomes the screen (below). With a base resolution and no `stretch`: `viewport` |
| `aspect` | `keep`, `expand` | When the screen's shape differs from the base's: black bars, or more of the world |
| `integer_scale` | `true` (default), `false` | `viewport` only: scale up in whole steps (each game pixel exactly k × k screen pixels) |
| `render_scale` | 0.25 to 2 (default 1) | `off` and `canvas`: draw the 3D scene smaller (or larger) and scale it to the picture, for speed |

Built games read the same file (`noor_cli build` copies the project's `Settings`). The player's `--window WxH`,
`--windowed` and `--display <file.json>` override it (tests use them).

## Stretch modes

**`viewport`: pixel art.** The scene is drawn at the base resolution (320 × 180, say) and scaled up with nearest
filtering, so every game pixel becomes a block of whole screen pixels. With `integer_scale` the block is always a whole
number: at 1280 × 720 each game pixel is 4 × 4; at 1920 × 1080 it's 6 × 6; at 1366 × 768 (4.27×) it stays 4 × 4 and the
picture is centred with thin black bars, rather than some pixels being 4 wide and some 5. This is what makes a game
"look like a lower resolution" at full screen. Godot calls it the same: the `viewport` stretch mode.

**`canvas`: sharp, but laid out for the base.** The scene is drawn at the screen's own resolution, in a rectangle of the
base's shape. Good for smooth 2D art and for 3D games: the camera shows the same part of the world at any resolution,
just sharper. (Godot's `canvas_items`.)

**`off`.** The scene is drawn at the window's size; a bigger window shows the same view in more pixels.

**`aspect`**: with `keep`, a screen of another shape gets black bars (16:9 game on a 16:10 screen: bars above and below).
With `expand` the bars are filled with more world instead: in `viewport` mode, more game pixels of the same size (320 ×
200 at 1280 × 800); the camera's height (or field of view) grows to match, so the middle of the picture is exactly what
`keep` shows, never squashed (tested pixel for pixel).

**The HUD** (`noor.ui`) is placed in the game's picture: its 720 units of height ([The HUD](../scripting/LUA_API.md#the-hud))
are the picture's height, not the window's, so it never spills into the bars. (It was already the same size at any
resolution; part 6 only moved it into the picture. An earlier version of this page said the base resolution changed
its size: it doesn't.) It's drawn at the screen's resolution, crisp, not blocky.

**What pixel-art games may want to turn off.** The scene is still anti-aliased (MSAA) and tonemapped at the base
resolution. Sprites with `"filter": "nearest"` and `"unlit": true` come out exactly as painted; lit, rotated or 3D things
get soft edges within game pixels, which usually looks right. Turning MSAA off per project isn't a setting yet.

## Render scale (3D)

`render_scale` draws the 3D scene at a fraction of the picture's pixels and scales it up smoothly. On Hillside at
1920 × 1080 (an RTX 3080), 0.5 took the scene pass from about 1.1-1.6 ms to 0.4-0.65 ms; the sky, the tonemap and the HUD
don't shrink. It's for heavy scenes on modest GPUs: a game can offer it as a quality setting.

## Cameras for 2D

### Pixel snap

```json
"camera": {"projection": "orthographic", "height": 11.25, "pixel_snap": true}
```

The camera moves in whole pixels of the picture as drawn (its height divided by the picture's height in pixels), so a
scrolling pixel-art world doesn't shimmer. With `viewport` stretch that's whole game pixels.

### Following (`camera_follow`)

```json
"camera_follow": {
  "target": "Hero",
  "offset": [0, 1],
  "dead_zone": [3, 2],
  "smoothing": 0.2,
  "bounds": [-14, -8, 44, 14]
}
```

- **`target`**: an entity's name or id. The camera follows where it's drawn (physics' smoothed pose).
- **`offset`**: metres added to the target's position (look a little above the hero).
- **`dead_zone`**: a box (width, height in metres) round the camera's centre where the target moves freely. When it
  leaves the box, the camera moves just enough to bring it back to the edge.
- **`smoothing`**: seconds. How quickly the camera closes the gap (an exponential approach that's the same at any frame
  rate). 0: at once.
- **`bounds`**: the level's left, bottom, right and top. The edges of the view stay inside them (a level narrower than
  the view: the camera is centred on it). The view's width comes from the camera's height and the display's shape
  (the base resolution's if kept, else the window's).

It works in the camera's XY plane (2D's), runs after physics and scripts each frame, and only moves the camera. A script
that wants something else (a camera that looks ahead, shakes, or follows two players) leaves `camera_follow` off and
moves the camera itself.

### Parallax layers (`parallax`)

```json
"parallax": {"factor": [0.2, 0.2], "anchor": [0, 5]}
```

An entity with `parallax` (and its children) moves with the camera by `1 - factor`: factor 1 is the world itself, 0
stays with the camera (a far sky), 0.2 drifts slowly (distant hills), 0.5 is in between. Where the camera is at
`anchor`, the layer sits exactly where it was placed, so lay out a layer with the camera there. Put the layer behind the
scene (a smaller Z) or on a sprite layer that draws first. A single number works for both axes (`"factor": 0.5`).

## Coming from Godot

| Godot | NoorEngine |
|---|---|
| Project Settings ▸ Display ▸ Window ▸ Size (viewport width / height) | `base_resolution` |
| Stretch mode `viewport`, `canvas_items`, `disabled` | `stretch`: `viewport`, `canvas`, `off` |
| Stretch aspect `keep`, `expand` | `aspect`: `keep`, `expand` (Godot's `keep_width` / `keep_height` aren't here yet) |
| Stretch scale mode `integer` | `integer_scale` |
| Scaling 3D scale | `render_scale` |
| Camera2D position smoothing, drag margins, limits | `camera_follow`: `smoothing`, `dead_zone`, `bounds` |
| Snap 2D transforms to pixel | the camera's `pixel_snap` (and `viewport` stretch) |
| ParallaxBackground / ParallaxLayer `motion_scale` | `parallax`: `factor` |

## Not yet

A display menu in the editor's project settings (edit the file for now); changing the window mode while a game runs
from a script; `keep_width` / `keep_height`; turning off MSAA per project; parallax that repeats (an endless sky).
