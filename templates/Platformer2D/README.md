# 2D Platformer

A small level to run and jump in, to build your platformer from:

- `Scenes/Main.noorscene`: a **tilemap** level (`Assets/Tiles/level.noortileset`: grass, dirt and stone are solid,
  planks are one-way), the player, a sky, and a camera that **follows** (dead zone, smoothing, the level's edges).
- The player is a `character` in the XY plane with the built-in **`platformer`** component: speed, jump height, coyote
  time and jump buffering are all on the component. `Scripts/Player.luau` is yours to grow.
- `Settings/display.json`: 320 × 180, scaled up in whole pixels.

Paint more level in the editor: the **2D** view and the **Tilemaps** tab. Docs: tilemaps, sprites, the platformer
character and your own controllers, in the repository's `docs/`.
