# NoorIsometric

A small torch-lit dungeon room in the classic 2:1 isometric view: the 2D foundations' isometric sample
([plan](../../docs/milestones/M9_2D_PLAN.md)). Coth, from Tom's game The Uncounted, walks round the pillars.

    noor_player samples/NoorIsometric

**W A S D** or the **arrows** walk (up is up on screen). Until you press a key, Coth walks a loop by himself.

What it's made of, all public features:

- An **isometric tilemap** (`"layout": "isometric"`, 2 × 1 m diamonds): floor flagstones and tall wall blocks, drawn by
  `tools/samples/make_noorisometric.py`. Nearer tiles hide the ones behind.
- **Coth**, an 8-direction sprite sheet. In a flat game the script says which way he faces on screen (`"facing"`), and
  the sheet shows that view. **`"y_depth": true`** gives him the map's depth rule, so a pillar in front of him hides
  him and he hides a pillar behind him, pixel by pixel ([Sprites](../../docs/fx/SPRITES.md)).
- **Walking on the floor only**: isometric maps don't collide (yet), so `Scripts/Walker.luau` asks the map which tile
  is under the next step (`e:tile(x, y)`) and slides along walls.
- **Lighting**: moonlight, four torches and a lantern that walks with Coth (point lights; sprites and tiles are lit).
- A **follow camera** and **display settings** for smooth art (`canvas`: sharp at any window size).
