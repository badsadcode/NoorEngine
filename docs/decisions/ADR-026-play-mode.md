# ADR-026: Play mode in the editor (before scripting)

**Status:** Accepted · **Date:** 2026-11-15 · **Milestone:** 3

## Context
Milestone 3's goal says the editor can "author, save, reopen and **play** simple scenes", and its
deliverables list play/stop. Scripting (C#/Lua) only arrives in Milestone 4, and there is no game
loop module yet. We need a play mode that is real (it runs something, and stopping it restores
the scene) without inventing the runtime architecture early.

## Options
- **A. A runtime copy inside the document.** Play copies the expanded scene; behaviours update
  the copy; Stop discards it. The authored scene and its undo history are never touched.
- **B. Serialize, run a separate player process, reload on Stop.** Closest to shipping builds,
  but needs the player/runtime host that Milestone 4 designs (input, scripts, build pipeline).
- **C. Mutate the authored scene and roll back with undo.** Simple, but fragile: one missed
  change corrupts the user's scene; Unity-style "changes in play mode are lost" becomes "changes
  might stick".

## Decision
**A** for Milestone 3:
- `noor_doc_play` makes `runtime_ = expand_prefabs(scene)`; `noor_doc_tick(seconds)` runs the
  built-in behaviours on it (Milestone 3 has one: `rotator`) and publishes render snapshots from it;
  `noor_doc_stop` discards it and republishes the authored scene.
- While playing: `noor_doc_scene` shows the running values (`"playing": true`), every edit, undo,
  redo and scene switch is refused with a clear error, and the editor hides gizmos and makes the
  Inspector read-only. Saving still saves the authored scene.
- The host drives time (`tick` from a UI timer at ~60 Hz, panels refreshed ~5 Hz); a stalled
  frame advances at most 0.1 s.
- Entity ids are the same in the running copy (prefab contents use the same derived ids), so
  the selection survives Play.

## Consequences
- Play can never damage the authored scene: a test plays, refuses an edit, stops, and checks the
  scene is unmodified.
- Milestone 4 replaces the UI-timer tick with the runtime's own loop and adds scripts as
  behaviours; it may move play into a separate player process (option B) for build parity. The
  document-side contract (play/stop/tick, edits refused while playing) can stay.
- Changes made while playing are lost on Stop, as in most engines. A "keep play-mode changes"
  tool can come later if wanted.

## Reviewed by
Sage.
