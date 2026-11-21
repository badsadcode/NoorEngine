# NoorPlatformer

A short pixel-art platformer: the 2D foundations' sample ([plan](../../docs/milestones/M9_2D_PLAN.md)). Run, jump, collect
the eight coins, mind the spikes, and reach the door.

    noor_player samples/NoorPlatformer

**A / D** or **← / →** run, **Space** jumps (hold for higher), **↓ + Space** drops through a plank. Until you press
a key, an autopilot plays.

What it's made of, all public features you can use in your own game:

- A **tilemap** level (`Scenes/Level.noorscene`, tileset `Assets/Tiles/level.noortileset`): solid stone and brick,
  one-way planks, decoration. Edit it in the editor's 2D view with the Tilemaps panel.
- The built-in **`platformer`** character (speed, jump height, coyote time, jump buffering, animations).
- **Triggers**: coins, spikes and the door are trigger colliders; `Scripts/Hero.luau` reacts in `on_trigger_enter`.
- A **HUD** (`noor.ui.text`, `noor.ui.rect`), laid out in the game's 320 × 180 pixels.
- **Display settings** (`Settings/display.json`): drawn at 320 × 180 and scaled up in whole steps (4 × 4 screen pixels
  per game pixel at 1280 × 720), black bars where the shape differs ([Display](../../docs/rendering/DISPLAY.md)).
- A **follow camera** (dead zone, smoothing, the level's bounds, snapped to pixels) and **parallax** mountains and pines.

The art is Tom's own, hand-drawn for an old game of his and used here with his permission. The level, scene and script
are written by `tools/samples/make_noorplatformer.py`.
