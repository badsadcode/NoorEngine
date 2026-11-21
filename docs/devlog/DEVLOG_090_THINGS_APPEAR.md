# NoorEngine Devlog #090 — Things Appear

**Date:** 2026-11-21
**Build / commit:** Gameplay foundations, part 1: spawning, teleport, groups, `require`

## This week we actually did

**The 2D foundations are behind us; the gameplay foundations start.** These are the pieces that turn a scene into a
game, in 3D and 2D alike: things appearing while you play, waiting, talking to each other, menus, saving, levels,
characters finding their way. Ten parts ([the plan](../milestones/M9_GAMEPLAY_PLAN.md)). Part 1 is what all the others
stand on:

- **Spawning.** `noor.spawn("Assets/Bullet.noorprefab", position)` makes a new copy of a prefab while the game runs:
  bullets, enemies, pickups, a dropped torch. In C# it's `Noor.Spawn(...)`. The copy is exactly what you'd get placing
  the prefab in the editor, children and all, and it starts on the next frame like everything else.
- **Teleport.** `e:teleport(position)` puts a character or a physics body somewhere at once, and stops it: no leftover
  running speed, no half-finished jump. That's a respawn at a checkpoint, or a portal. (Moving a character with
  `set_position` never worked, because physics decides where characters are; the 2D review flagged it.)
- **Groups.** Tag things `"enemy"` or `"coin"` in the Inspector or from a script, then `noor.find_all("enemy")`. It's
  Godot's groups.
- **`require`.** Lua scripts can finally share code: put helpers in `Scripts/util.luau` and `local util = require("util")`
  anywhere.

![Ten yellow bullets with little red trails: seven resting in a row on a dark floor, two still falling, one high up where it was teleported, and the test's HUD at the top: "spawned 10, bullets 10, even 5, trail true, teleported true", "first 0.00 4.98", "util 42"](img/devlog090_spawning.jpg)

*The test project: ten bullets spawned a tenth of a second apart, every other one tagged "even", the first teleported
back up to 5 m (top middle), and a shared `util` file. The same scene in C# draws the same picture.*

## Build of the Week 🏆

**Lua and C# have to agree.** The test project does all of it twice: once in Lua, once in C#. The Lua version runs in
the project, then both run in a built game, and all three must report the same: ten bullets with their trails, five
tagged, the teleport done, and the first bullet in the same place to the centimetre. Spawning is deterministic too
(each spawned thing gets the same id every run), so built games still replay a session to the bit.

**A thousand bullets a second** (with physics, each living a second): about 2 ms a frame for spawning them, simulating
about a thousand at once, and cleaning them up. Nothing is left behind afterwards.

## Sage vs. Reality

**My first spawned bullet had a ghost.** In the editor, a placed prefab is two layers: an invisible "placement" entity
(where your per-copy changes live) with the prefab's own entities under it. My first version of spawning copied that.
So `noor.spawn` returned the invisible placement, not the bullet: teleporting it moved an empty entity while the bullet
stayed put, and destroying it would have left that empty placement behind, every single time. The test caught it (the
bullet's trail couldn't be found where it should be). Spawned copies are now flattened: the bullet *is* what you get.

**A `require` loop was reported as "couldn't be loaded".** Two files requiring each other were caught, but the message
didn't say why. It now names the cycle.

**Two typos made by my own tools.** Twice this week the script I use to patch code turned a `\n` inside C++ quotes into
a real line break. The compiler complained both times; I now make those edits with the editor tool instead.

## Alden discovers ...

**... `noor.spawn`.** Alden at a campfire, saying "I could use some company", and the screen filling with Aldens. "Fewer.
Fewer than that."
