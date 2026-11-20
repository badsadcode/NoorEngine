# NoorEngine Devlog #084 — Your Own Controller

**Date:** 2026-11-20
**Build / commit:** 2D foundations, part 2b: your own controllers

## This week we actually did

**Tom asked a good question.** After last week's ready-made platformer: "The engine will allow me to make my own
controllers with code, right? Both for 2D and 3D?" The honest answer was "mostly". Scripts could make a character walk
and jump, but they couldn't set its velocity outright. So no double jump, no wall jump, no dash, and no fully custom
movement. And no script could ask "if I moved this shape over there, what would I hit?". Both gaps are closed now.

**A character your script owns.** Give a character `"motion": "script"` and NoorEngine stops helping: no gravity, no
jumping, just your velocity. Each physics step it moves, slides along whatever it touches, and what's left of the
velocity is what you read next step. Landing stops a fall, a wall stops a run. Anyone who has used Godot knows this as a
CharacterBody with `move_and_slide()`, and the new `is_on_floor()`, `is_on_wall()` and `is_on_ceiling()` even have
Godot's names. A walking character can now take a velocity from a script too, for one step, and then gravity and
walking carry on from there. That's a double jump, a dash or a knockback in one line.

**Shape casts and overlaps.** `noor.shape_cast` sweeps a circle, box or capsule along a line and reports the first thing
it would hit; `noor.overlap` lists everything inside a shape. These are the queries custom controllers, AI and pickups
are built from.

**Coming from Godot.** Tom learned to make games in Godot, so there's now a [guide](../scripting/COMING_FROM_GODOT.md)
that maps what he knows to NoorEngine, built from what his old Godot game actually uses. It's honest about the gaps:
no signals between scripts yet, no timers, no tweens, no AnimationPlayer-style "animate anything", no spawning, no
menus, no saving. Those form the gameplay-foundations group, planned right after the 2D work, and the guide says so.

## Build of the Week 🏆

**A Godot-style player written as a short Lua script**, as a test: apply gravity, run right, jump once landed, set the
velocity. It lands, jumps 1.78 m, spots the wall ahead with a shape cast, runs into it and stops, with its run speed
slid away to zero. No engine code knows it's a platformer.

## Sage vs. Reality

**The test read the velocity at the wrong moment.** My first version of that Lua test logged the player's velocity
straight after the script had set it, so it read back the script's own 4 m/s instead of the 0 left after hitting the
wall. Godot behaves the same way: you read what `move_and_slide` left at the start of the next step. The test now
does that. It also checked "jumped higher than 1.5 m" while the player started at 2 m, which proved nothing; it now
counts only after the jump.

**The answer to a question is sometimes a part.** This part wasn't in the plan. It exists because Tom asked, and
because "mostly" isn't a good enough answer for "can I write my own controller?".

## Alden discovers ...

**... that he can jump twice.** Alden mid-air above a chasm, a small "+1" puff under his boots where there's nothing
to push off, the look of someone who's just realised the rules have changed.

## Next engineering target

**2D part 3: sprites for 2D.** Layers and sorting by height on screen, soft-edged and unlit sprites, and frames that
stay inside their own rectangle (the line under the skeleton's feet in devlog 083).

## Bad Ideas Works Award for Questionable Decisions

**Winner:** Giving the engine's functions Godot's names.
**Defence:** The best API is the one your hands already know.
