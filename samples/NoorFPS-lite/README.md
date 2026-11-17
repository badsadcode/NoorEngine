# NoorFPS-lite

The Milestone 4 sample: a small first-person target shooter made only with NoorEngine's public
script API. Walk, sprint, jump, look around, shoot the gold targets before the minute is up.
Sounds, a HUD, physics crates to knock about.

**Open it:** NoorEditor ▸ Open project ▸ this folder, then **Play** (or File ▸ Build Game).

| | |
|---|---|
| Click | Start / shoot |
| WASD, Shift, Space | Walk, sprint, jump |
| Mouse | Look |
| Esc | Free the mouse (pauses the round) |

Before the first click the game plays its **demo**: it turns to the nearest target and shoots by
itself. The automated tests use that (`tests/sample_fps_check.cmake`): no input, 15 seconds, and
the score on the HUD must be at least 10.

## Two languages, one game

- `Scenes/Main.noorscene`: the scripts in **C#**, `Scripts/FpsPlayer.cs` and `Scripts/Shooter.cs`.
- `Scenes/Main_Lua.noorscene`: the same arena with the **Lua twin**, `Scripts/FpsPlayer.luau` and
  `Scripts/Shooter.luau`. Same rules, same HUD, line for line where the languages allow.

`FpsPlayer` (on the Player) moves and looks; `Shooter` (on the Camera) is the game: it shoots,
scores, runs the clock and draws the HUD. They talk through the Camera's `game` component
(`{ "state": "demo" | "playing" | "paused" | "over" }`), which `Shooter` writes and `FpsPlayer`
reads.

## Assets

`Assets/Audio/*.wav` were synthesised for this sample (a noise crack, a metallic ping, a soft
thud, two tones) and are MIT like the rest of the repository.

Since Milestone 5 the arena is textured: brick walls, a worn concrete floor, concrete cover,
blue metal pillars and wooden crates. The texture sets are CC0 from [Poly Haven](https://polyhaven.com)
(Castle Brick 07, Concrete Floor Worn 001, Blue Metal Plate, Wooden Rough Planks), shrunk to
512² for this sample; the materials were made with **Make material**, and the tiled ones
(`Assets/Materials`) are instances that only change `uv_scale`.
