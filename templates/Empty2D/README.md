# Empty 2D

A clean start for a 2D game:

- `Scenes/Main.noorscene`: an orthographic camera (2D: the XY plane, x right, y up, metres) and one sprite.
- `Scripts/Main.luau`: a script on the sprite. It turns it and writes on the screen.
- `Settings/display.json`: drawn at 320 × 180 and scaled up in whole pixels, crisp pixel art at any window size.
  Change `base_resolution`, or `"stretch": "canvas"` for smooth art.
- `Assets/Sprites/icon.png`: 16 pixels a metre, nearest filtering.

Next: the [2D docs](https://github.com/badsadcode/NoorEngine/blob/main/docs/milestones/M9_2D_PLAN.md) (sprites,
tilemaps, physics), and [Coming from Godot](https://github.com/badsadcode/NoorEngine/blob/main/docs/scripting/COMING_FROM_GODOT.md).
