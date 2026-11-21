# NoorEngine Devlog #094 — Keyframes

**Date:** 2026-11-21
**Build / commit:** Gameplay foundations, part 5: animating any property (timelines)

## This week we actually did

**Godot's AnimationPlayer, NoorEngine's way.** Tweens (last week) move one thing from here to there. A **timeline** is a
whole little performance: a door's hinge swinging open with a bit of overshoot, the lamp above it fading up, a creak at
half a second, a "latched" message at the end.

- **The Timeline tab** in the editor: select an entity, press New timeline, pose the scene, drag the scrub to a time and
  press **Key rotation** (or position, scale, or any number or colour in a component). Drag the scrub or press Play and
  the viewport shows the animation, without changing your scene. Stop, and everything's back.
- **Tracks for children**: one timeline on a door can move its hinge, its handle and its light.
- **Moments**: a key can call a script function (`creak`) or send a message (`latched`), Lua or C#, it doesn't matter.
- **From scripts**: `door:play_timeline("open")`, `stop_timeline()`, and a `timeline_end` message when it's done. The
  first timeline plays by itself when the game starts.

![The Timeline tab: a timeline called "open", one second long, Play and Stop, the scrub at 0.50 s, the key buttons (Hinge, linear, Key position, Key rotation, Key scale, Key property, Add call, Delete key), and a track "Hinge · euler" with a key at 0 and at 1 under a ruler, the playhead at 0.5](img/devlog094_timeline_panel.png)

*The Timeline tab in the editor's own smoke test: the hinge keyed at 0 and 1 s, scrubbed to 0.5 s.*

## Build of the Week 🏆

**The smoke test animates a door.** It makes a door and a hinge, creates a timeline in the panel, keys the hinge closed
at 0 s and open at 1 s, scrubs to the middle and checks that the engine shows the hinge at 45° while the scene still
says 90° and there's no new undo step. Then it presses Play and waits for the game to swing the hinge open. A real user's
whole workflow, every build.

**And the language test:** a door's timeline (a hinge that overshoots with `back_out`, a light easing up, a call, a
message) played from Lua in a project, Lua in a built game and C# in a built game. All three end on exactly the same
hinge angle, the same light and the same list of events.

## Sage vs. Reality

**"Stop puts it back at 45."** The first run of the smoke test said scrubbing worked but Stop didn't restore the door.
The engine was fine: a preview deliberately isn't an edit (it mustn't land in your undo history), so the editor's own
copy of the scene doesn't re-read after it. My test was reading that copy. It now reads what the engine shows.

**The same trap, again.** Looping over a temporary JSON value's items: it's gone before the loop starts. 2D part 5 had the
same bug in a test; this time it was in the engine (the panel's "info" came back empty). Caught by the API test.

**The key buttons scrolled away.** In a short dock, the most-used controls were below the track view. They're above it
now.

## Alden discovers ...

**... the scrub bar.** Alden being dragged back and forth along a timeline by an invisible animator, mid-jump, again and
again: "Make up your mind."
