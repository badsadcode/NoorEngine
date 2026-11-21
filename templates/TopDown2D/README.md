# Top-Down 2D

A meadow to walk round, to build a top-down game from (an adventure, a farm, a dungeon crawler):

- `Scenes/Main.noorscene`: a **tilemap** (`Assets/Tiles/meadow.noortileset`: water, trees and rocks are solid), the
  player, and a camera that **follows** inside the map's edges.
- The player is a `character` in the XY plane with **`"motion": "script"`**: `Scripts/Player.luau` owns its velocity
  (Godot's CharacterBody2D way), so it walks in every direction and slides along what's solid. The sprite sheet has
  four views; the script's `"facing"` picks one.
- `Settings/display.json`: 320 × 180, scaled up in whole pixels.

Paint the map in the editor: the **2D** view and the **Tilemaps** tab.
