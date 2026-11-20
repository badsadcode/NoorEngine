# Tilemaps

2D foundations, part 4 ([ADR-040](../decisions/ADR-040-2d-and-isometric.md)). A tilemap is a grid of tiles cut from a
sprite sheet: the ground and walls of a platformer, the floors of a top-down dungeon, the diamonds of an isometric
map, the hexes of a strategy game. It's drawn in chunks (one draw per 32 × 32 cells, however many tiles), and square
maps collide.

## The tileset (`.noortileset`)

```json
{
  "noor_tileset": 1,
  "name": "Cave",
  "sheet": "<the .noorsprite id>",
  "solid": [0, 1, 2, 3, 6],
  "one_way": [4]
}
```

- **The tiles are the sheet's frames**, numbered left to right, top to bottom, as sprites count them. The sheet's
  `pixels_per_metre` sets how big a tile is in the world, its `pivot` where it sits on its cell, its `filter` whether
  pixel art stays crisp.
- **`solid`** tiles collide on every side; **`one_way`** tiles are platforms you stand on from above and jump up
  through (2D part 2). Everything else is decoration.

## The `tilemap` component

```json
"tilemap": {
  "tileset": "<the .noortileset id>",
  "layout": "square",
  "cell": [1, 1],
  "rows": [
    ". . . 4 4 4 . .",
    ". 5 . . . . 6 .",
    "0 0 0 0 0 0 0 0"
  ]
}
```

- **`rows`**: one line per row, **top row first**, so the text looks like the map. `.` is empty; a number is a tile.
  Rows can be different lengths. Cell (0, 0) is the **bottom-left**, x to the right, y up (the 2D world's axes), at the
  entity's origin. Readable, and a change to one row is a one-line diff.
- **`layout`**:
  - `square`: cell (x, y) at (x × cell.x, y × cell.y).
  - `isometric`: the usual 2:1 diamonds. x runs up and to the right, y up and to the left (`cell` is the diamond's
    width and height, e.g. `[2, 1]`). Tall tiles (walls, trees, blocks) rise above their diamond and hide what's
    behind them: tiles further back are drawn first.
  - `hexagonal`: pointy-topped hexes, odd rows shifted half a cell right, rows ¾ of a cell apart.
- **`layer`, `order`, `sort`, `unlit`, `color`**: as for sprites ([Sprites](SPRITES.md#drawing-order-in-2d)). A tilemap
  on layer −1 sits behind characters on layer 0.
- **`collision`**: `true` by default. Square maps collide; isometric and hexagonal ones don't (yet): what's walkable in
  those is usually a game's own rule.
- **`cast_shadows`**: `false` by default.

## In the editor

The **Tilemaps** tab at the bottom of the editor, beside Water (2D foundations, part 5):

1. Pick a tileset and press **New tilemap**: a one-cell map at the origin, selected.
2. Turn on the **2D** view in the viewport's toolbar: straight down at the XY plane, flat. The right or middle button
   pans, the wheel zooms where the pointer is, F frames the selection.
3. Pick a tile from the palette (the tileset's own pictures; the eraser is first) and drag in the viewport. Every cell
   the pointer crosses gets that tile, the cell under the pointer is outlined, and **a whole drag is one undo step**.
4. Paint past the edge and the map grows. Grow it to the left or downwards and the entity moves back by the cells
   added, so the tiles already there stay exactly where they were.
5. **Snap to tiles** and **Snap to pixels** set the viewport's snapping, for moving things onto the grid or onto
   whole pixels of the art.

Choosing the Move, Rotate or Scale tool puts the brush down. The panel paints through the editor API
(`noor_doc_tilemap`), so tools and tests can paint exactly as a person does; games change tiles with `e:set_tile`.

## Collision

The solid tiles of a square map become as few boxes as cover them exactly: each row's runs, grown upwards while the
rows above match. A floor 200 tiles long is one box, not 200. One-way tiles become one-way platforms the same way.
When the map changes (a script breaks a block), its boxes are rebuilt at the next physics step.

## From scripts

```lua
local t = e:tile(x, y)        -- the tile there (-1 empty), nil if e has no tilemap
e:set_tile(x, y, -1)          -- break it (any number places that tile); true if (x, y) is inside the map
```

C#: `entity.Tile(x, y)`, `entity.SetTile(x, y, tile)`. Drawing and collision follow at once.

## What it costs

A 256 × 256 map (about 46,000 tiles, `Tilemap_Big` in the test scenes) is 64 draws (one per 32 × 32 chunk), about
0.6 ms of CPU a frame on the test machine, and its chunks are built once, then again only when its rows change. The
rows are fingerprinted each frame to notice changes; for very large maps that fingerprint is most of the cost.

## Not yet

Rectangle and flood-fill tools; picking a tile from the map (an eyedropper); animated tiles; auto-tiling (picking the right edge and corner tiles by
themselves); collision for isometric and hexagonal maps; several tilesets in one map (use one tilemap per tileset).
