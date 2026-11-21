# Gameplay foundations: plan

**Where it sits:** after the 2D foundations ([review](M9_2D_REVIEW.md)), before 9A part 2 ([why](M9_PLAN.md#the-order-from-here-sage-2026-11-20)).
Tom approved the order (2026-11-20) and this group (2026-11-21). **Principle 0 holds:** every piece is optional; a
project that uses none of it pays nothing (the yardsticks guard that).

**Goal:** what turns a scene into a game, in 3D first and in 2D too: things appear and disappear while you play,
things wait, talk to each other and move smoothly, there are menus, the game remembers, there's more than one level,
and characters find their way. Lua and C# get the same features (the rule since Milestone 4).

**Gate:** each piece has editor authoring where a person would author it, runtime behaviour, serialization, docs,
performance notes and tests (the Milestone 8 gate, kept); built games play to the bit; the samples grow into games
that use all of it; every existing golden and yardstick unchanged.

## Where it starts

- **Have:** `World::spawn` and `e:destroy` (engine side; scripts can destroy, not spawn); prefabs as assets, expanded
  when a scene loads; entity `tags` (no script API); engine events as hooks (`on_collision_enter`, `on_trigger_enter`,
  `on_animation_end`...); the immediate HUD (`noor.ui.text`, `noor.ui.rect`); model clips and controllers (8C), sprite
  animations (8B); keyboard and mouse input with actions and axes; deterministic fixed steps; built-game checks.
- **Haven't:** spawning from scripts; teleporting characters; `require` (one file per script, no shared code);
  timers, waiting, pause; messages between scripts; tweens; animating any property; menus and widgets; **gamepads (none
  at all)**; saving; changing level; navigation.

## Parts (each ends tested, documented and pushed)

| Part | What | Proves |
|---|---|---|
| 1 | **Spawning and the things around it**: `noor.spawn(prefab, position [, rotation, parent])` and C#'s `Spawn`, returning the new entity (its scripts, bodies, sprites, sounds start the next frame); `e:teleport(v)` for characters and bodies (velocity and contacts cleared); **groups**: `noor.find_all(tag)`, `e:has_tag`, `e:add_tag`, `e:remove_tag`; Lua **`require`** for shared script files | A spawned prefab is the same as one placed in the editor (its components, to the bit); 1,000 bullets spawned and destroyed a second, measured; a respawn at a checkpoint; built game identical |
| 2 | **Time**: timers (`noor.after(seconds, fn)`, `noor.every`), waiting inside a hook (`noor.wait(seconds)`, Luau coroutines; C#'s `await`), **pause** (the game's clock stops, physics and animation with it; UI and real-time timers go on), time scale (slow motion) | Timers fire on the right fixed step, the same in a built game; a paused game's world doesn't move while its pause menu does; coroutines stopped cleanly when their entity is destroyed |
| 3 | **Messages between scripts** (Godot's signals): `e:emit("opened", data)`, `e:on("opened", fn)` (listening to another entity), `noor.on(...)` for game-wide messages; calling a function on another entity's script (`e:call("open")`); engine events as messages too; listeners tidied away when either side is destroyed | Order of delivery is the order sent, within a frame, deterministic; a door opened by a lever two entities away; nothing leaks after 10,000 spawn-and-destroy cycles |
| 4 | **Tweens**: `noor.tween(e, "position", to, seconds, "ease_out")` for transforms, colours, a component's numbers; sequences and parallel groups; on finish callbacks; easing functions | Every easing against its formula; a tween lands exactly on its target; tweens on a destroyed entity stop silently; built game identical |
| 5 | **Animating any property** (Godot's AnimationPlayer): a `.noortimeline` asset of keyframed tracks (a door's rotation, a light's colour, a component's field, a function call at a moment) played by an `animation_player` component; an editor Timeline panel to key and scrub | A timeline plays the same values as its keys (interpolated by hand); events fire once each; the editor keys a door opening and the game plays it |
| 6 | **Game UI**: retained widgets (panel, label, button, image, slider, toggle, list, text box), layout (anchors, stacks, grids), themes, **focus and keyboard / gamepad navigation**; `.noorui` files built in a UI editor or from scripts; it lives in the display's picture (2D part 6) | A pause menu and an inventory made in the editor work with mouse, keyboard and gamepad; layout at three resolutions by readback; cost of 500 widgets measured |
| 6b | **Gamepads and rebinding**: SDL gamepads in the input map (buttons, sticks, triggers, dead zones), several players, hot-plugging, rebinding at run time from a settings menu (saved) | A simulated gamepad drives the platformer and the menus; a pad unplugged and back; rebinding survives a restart |
| 7 | **Saving and loading**: `noor.save(slot, table)` / `noor.load(slot)` (versioned JSON in the user's save folder, written atomically, a backup kept); a `persist` component that saves chosen entities' state (transform, chosen fields) and restores it; settings saved apart from games | A save from one version loads in the next (a migration test); a crash while saving loses nothing (the atomic write, simulated); a level restored to the bit |
| 8 | **Levels**: `noor.change_scene(path)` with a fade and a loading screen, additive scenes (load a room into the level), entities that survive a change (`"keep": true`, Godot's autoload), spawn points | Three levels walked through and back with the game's state intact; no leaks across 100 changes; the first frame after a change measured |
| 9 | **Navigation and agents**: navmeshes baked from colliders and terrain (Recast/Detour, zlib licence), a `nav_agent` component (path following, simple avoidance, off-mesh links for jumps and ladders), grid paths for tilemaps (2D, isometric); the navmesh drawn in the editor, baked by a button and by `noor_cli` | Agents reach their goals round obstacles in 3D and on a tilemap; paths against hand-made answers; a crowd of 100 agents measured; built game identical |
| 10 | **Samples, templates and review**: NoorPlatformer grows a respawn, a pause menu, a second level and a saved best time; NoorIsometric grows enemies that find their way to Coth and a door to a second room; a 3D sample (Alden on Hillside, or NoorFPS-lite) gets a menu, saving and a companion who follows by navmesh; templates **First-Person** and **Third-Person** (spec 24.1), made of these | The gate |

Why this order: spawning, time and messages (1–3) are what every later part is built from; tweens and timelines (4–5)
make things move without code per frame; UI (6) needs time (pause) and messages (buttons); saving (7) needs to know
what's in the world; levels (8) need saving to carry state across; navigation (9) is the biggest and the most
independent, so it goes last before the samples tie everything together.

## Highest risks (settle early)

1. **UI: our own or a library.** A retained UI with layout, focus, gamepad navigation, text input and themes is a big
   system. RmlUi (MIT; HTML/CSS-like documents, used in shipped games) would bring layout and styling at once, but
   its own document format and a second text renderer beside ours. Our own, on the HUD we have, keeps one look, one
   font system and the editor's style. **An ADR in part 6 decides, from a prototype of the same pause menu both ways.**
   My lean is our own, kept small, with RmlUi as the fallback if layout becomes a project of its own.
2. **Coroutines and determinism.** `noor.wait` must resume on fixed steps, not frames, or built games drift. A
   paused coroutine can't be saved (part 7 saves data, not running code), which the docs must say plainly.
3. **Navigation: Recast/Detour** (zlib) is the standard and the safe choice; the risk is baking time on Hillside-sized
   terrain and later on 9A's streamed worlds (tiled navmeshes from the start, so part 9 doesn't block 9A).
4. **Saving what's in a world.** Generated content (9B–9E) is regenerated, not saved; only what changed is (ADR-039's
   rule). Part 7's `persist` must fit that from the start.
5. **Small-game cost.** Each part is measured on the yardsticks: a project using none of it gets no new threads,
   memory or frame time.

## Open questions

1. **UI authoring:** a visual UI editor (drag widgets, like Godot's Control nodes) is the plan; is a code-first way
   (build menus from Lua, like an immediate-mode GUI) wanted as well, or only for debugging tools? (My call: both,
   since the HUD already is code-first.)
2. **Which 3D sample grows in part 10:** Alden on Hillside (a third-person adventure: a companion, a quest-ish task,
   saving) or NoorFPS-lite (a shooter: enemies with navigation, a menu)? Tom leans 3D; I'd take Alden on Hillside,
   because it also exercises water, animation and the big-world path 9A takes next.

## Not here (later)

Networking and multiplayer (spec: its own milestone); dialogue trees and quest systems (game-level, built from these
parts); behaviour trees and state machines for AI beyond navigation (after part 9, when a game needs them); localisation
of UI text (with the UI's text pipeline, after part 6); 2D lighting and normal-mapped sprites (ideas inbox).
