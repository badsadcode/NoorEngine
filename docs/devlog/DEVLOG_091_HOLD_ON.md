# NoorEngine Devlog #091 — Hold On

**Date:** 2026-11-21
**Build / commit:** Gameplay foundations, part 2: timers, waiting, pause, slow motion

## This week we actually did

**Games can wait now.** Until today, a script that wanted something to happen "in two seconds" had to compare
`noor.time()` itself, every frame. Now:

- **Timers.** `noor.after(2, fn)` calls `fn` once, two seconds from now. `noor.every(0.5, fn)` keeps calling it until you
  cancel it or it returns `false`.
- **Sequences that wait.** `noor.start(function() ... noor.wait(1) ... end)` reads like a little script for a moment in
  the game: open the door, wait a second, play the creak. C# gets the same with `await Wait(1)`.
- **Pause.** `noor.set_paused(true)` stops the game's clock: physics, animation, particles, cameras and timers all
  freeze. Scripts ticked **Runs while paused** in the Inspector keep going, which is exactly what a pause menu needs.
  The game's HUD (the score, the health bar) stays on screen underneath the menu.
- **Slow motion.** `noor.set_time_scale(0.25)`. Physics still steps in the same fixed-size steps, just fewer of them,
  so it stays deterministic.

Everything belongs to the entity that made it, so a timer can't outlive the enemy that set it.

## Build of the Week 🏆

**A test project where every number is checked.** A timer, a repeating timer that stops itself, a sequence that waits
twice, a pause for exactly half a real second with a falling box that must not move during it, then slow motion. Lua in
the project, Lua in a built game and C# in a built game must show *exactly* the same HUD: `after 0.500, ticks 4, waits
0.300 0.800` and `paused 0.500 real s, frozen true, scale 0.50, game 1.742`. They do.

## Sage vs. Reality

The test project found four bugs before anyone else could:

- **0.5 seconds isn't 0.5 seconds.** Add 1/60 thirty times on a computer and you get 0.49999999999999994. So every
  half-second timer waited one extra frame. A hair of tolerance fixes it.
- **Pause let physics take one more step.** A script paused the game during its update, but that frame's physics
  had already been told how much time passed. The falling box moved one step after "pause". Now a pause applies to the
  rest of the frame straight away.
- **Lua and C# disagreed by one frame.** Lua ran every script's timers before anyone's update. C# ran each script's
  timers just before its own update. So when the pause menu's "resume" timer fired, a C# game skipped one more frame
  than the Lua one. C# now does what Lua does.
- **The score vanished under the pause menu.** The HUD is redrawn by scripts every frame, and paused scripts don't run.
  Now the last HUD they drew stays on screen while paused.

And my own test names tripped our own guard twice: a `;` in a test's name makes CMake's test list split it and run
nothing. The build refused, as designed.

## Alden discovers ...

**... the pause button.** Alden mid-leap over a stream, frozen in the air, looking at the camera: "Take your time."
