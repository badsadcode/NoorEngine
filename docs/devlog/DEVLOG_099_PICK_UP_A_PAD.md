# NoorEngine Devlog #099 — Pick up a pad

**Date:** 2026-11-21
**Build / commit:** Gameplay foundations, part 6e: gamepads and rebinding

## This week we actually did

**Gamepads.** Plug in an Xbox, PlayStation or Switch pad, or almost any other, and it works. Every game's default
controls already include it: the left stick or d-pad moves, South (Xbox A, PlayStation ✕) jumps, Start pauses, the
triggers aim and fire. NoorPlatformer plays with a pad without changing a line.

- **Up to four players.** Player 1 is the keyboard and mouse together with the first pad; players 2 to 4 are the next
  pads. Scripts ask `noor.input.pressed("jump", 2)` for player 2.
- **Plug and unplug any time.** A pad pulled out and plugged back in is the same player again. Scripts hear about it
  (`pad_disconnected`, so a game can pause when a controller's battery dies).
- **Sticks feel right.** A round dead zone (a worn stick that rests slightly off-centre doesn't drift) and smooth values
  past it, so half a push walks and a full push runs.
- **Menus follow the pad.** The d-pad or stick moves between buttons, South presses, East is Escape.
- **Rebinding.** A settings menu can ask "press a key for Jump" (`last_pressed`), swap it in (`rebind`), and save it.
  The player's changes go to their own settings file and come back at the next start; the game's own bindings stay
  as they shipped, so "reset to defaults" always works.

And with that, **part 6 is done**: UI pages built in the editor, with effects, driven by scripts and by any input.

## Build of the Week 🏆

**A pad that isn't there.** Tests can't plug in a controller, so noor_player can play a simulated one from a script:
plugged in at frame 1, Down on the d-pad at frame 10, South at 20, unplugged at 40, back at 60, a second pad at 85. The
test menu logs everything, and it reads the same from Lua in the project and from Lua and C# in a built game:
`click options; jump 1; gone 1; connected 1; click quit; jump 1; connected 2; jump 2`. The same pad runs the
platformer's hero *left*. The sample's autopilot only ever runs right, so that can only be the pad.

## Sage vs. Reality

**My own guard caught me.** A test name had a semicolon in it. The build refuses those, since ctest would silently run
nothing. I put that check there after it once hid 15 tests, and I'm glad it was there.

**A sentence I took back.** My first write-up said reading a real pad through SDL "was checked by hand". It wasn't: I
don't have a pad on this machine, and the tests use the simulated one. What I could check is that the player starts
cleanly with real input and no pad. The rest needs Tom and a controller.

## Alden discovers ...

**... the second pad.** Two players: Alden on the keyboard, a friend on the pad. The friend rebinds Jump to Start. Alden
pauses the game every time he means to jump. Nobody saves the bindings, out of mercy.
