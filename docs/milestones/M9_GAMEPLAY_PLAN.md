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
| 1 ✅ | **Spawning and the things around it**: `noor.spawn(prefab, position [, rotation, parent])` and C#'s `Spawn`, returning the new entity (its scripts, bodies, sprites, sounds start the next frame); `e:teleport(v)` for characters and bodies (velocity and contacts cleared); **groups**: `noor.find_all(tag)`, `e:has_tag`, `e:add_tag`, `e:remove_tag`; Lua **`require`** for shared script files | A spawned prefab is the same as one placed in the editor (its components, to the bit); 1,000 bullets spawned and destroyed a second, measured; a respawn at a checkpoint; built game identical |
| 2 ✅ | **Time**: timers (`noor.after(seconds, fn)`, `noor.every`), waiting inside a hook (`noor.wait(seconds)`, Luau coroutines; C#'s `await`), **pause** (the game's clock stops, physics and animation with it; UI and real-time timers go on), time scale (slow motion) | Timers fire on the right fixed step, the same in a built game; a paused game's world doesn't move while its pause menu does; coroutines stopped cleanly when their entity is destroyed |
| 3 ✅ | **Messages between scripts** (Godot's signals): `e:emit("opened", data)`, `e:on("opened", fn)` (listening to another entity), `noor.on(...)` for game-wide messages; calling a function on another entity's script (`e:call("open")`); engine events as messages too; listeners tidied away when either side is destroyed | Order of delivery is the order sent, within a frame, deterministic; a door opened by a lever two entities away; nothing leaks after 10,000 spawn-and-destroy cycles |
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

## Part 1 result (2026-11-21)
Done. **Spawning:** `noor.spawn(prefab, position [, euler, parent])` / `Noor.Spawn`, by asset id or path; the prefab
is expanded as an editor instance would be (nested prefabs and all), then **flattened**: its root takes the placement's
place, so the returned entity is the bullet itself and destroying it leaves nothing behind. Ids are deterministic (the
scene's id and a spawn count), so built games stay identical. **Teleport:** `e:teleport` / `Teleport`, through physics
for characters (velocity, run, jump, buffered jump, coyote time and contacts reset; a 2D character stays in its plane)
and bodies (velocities zeroed), anything else by its transform; the drawing's smoothing starts again from there.
**Groups:** `noor.find_all`, `e:has_tag` / `add_tag` / `remove_tag` (and C#). **`require`:** shared Lua files by name
or path, loaded once and shared, kept inside the project, cycles reported. rt API v13.

Proved: a spawned prefab is its root, placed and turned with its children, by id or path, the same ids in two worlds;
under a parent; started the next frame (unit); 1,000 bullets a second (about 1,000 alive, dynamic bodies) spawned,
simulated and destroyed at **about 2 ms a frame**, nothing left over (unit); a character teleported mid-jump stops,
keeps neither its run nor its jump, falls from rest and lands; a body stopped and turned (unit); tags (unit); `require`
shares one table, finds `Scripts/` by name, refuses paths outside and cycles, missing files are errors (unit); the same
spawning, tags, teleport and trail lookup from Lua in a project, Lua in a built game and C# in a built game, the first
bullet in the same place to the centimetre (`player.spawn_lua_csharp`).

Found on the way: an editor instance is a placement entity *above* the prefab's root (where overrides live); spawning it
that way would have left an empty entity behind every destroyed bullet and teleported the placement instead of the
body. Hence the flattening.

Not here: the samples' respawns and the like come with part 10, as planned.

## Part 2 result (2026-11-21)
Done. **Pause and time scale** in the World: paused, systems' update and late update get dt 0 and no fixed steps run, so
physics, animation, particles, cameras and game-time timers stop; time scale stretches the game's clock with physics
keeping its fixed step size; a real clock beside the game's; a change made during a frame applies to the rest of it.
**Runs while paused** (`"pause": "always"`, a checkbox in the Inspector): those scripts keep running on the real dt, and
the paused scripts' HUD stays on screen under them. **Timers** (`noor.after`, `noor.every`, cancellable, an `every`
returning false stops) and **sequences** (`noor.start` with `noor.wait`), owned by their entity, run before any
`on_update`, oldest first, on the game or the real clock. **C#:** `After`, `Every`, `await Wait`, `Noor.Paused`,
`TimeScale`, `RealTime` (rt API v14; the host runs every script's timers before any OnUpdate, as Lua does).

Proved: the clock (paused: no steps, dt 0, real time on; slow motion: half the steps; clamping), a pause made during
update freezes a falling body to the bit (unit); timers once on time, every until false or cancelled, a failing timer
stopped alone; sequences waiting and resuming at the right times, `noor.wait` outside one an error; pause: only
"always" scripts run, on the real dt, their real-time waits and timers, the game's timer resuming on the game clock;
timers and sequences gone with their entity, a failing or misbehaving sequence reported; the HUD under a pause menu
(unit); Lua in a project, Lua and C# in a built game showing exactly the same HUD: a 0.5 s timer at 0.500, waits at
0.300 and 0.800, a half-second real pause with the falling box frozen, slow motion after (`player.time_lua_csharp`);
the Inspector's checkbox (smoke).

Not as planned: timers are checked each frame against the game clock (first frame at or after their time), not on
fixed steps: simpler, and built games still replay them exactly. Found on the way: thirty steps of 1/60 s add up to
0.49999999999999994, which made 0.5 s timers fire a frame late (a hair of slack fixes it); a pause made in a script
used to let physics take one more step; C# ran each script's timers just before its own OnUpdate, so it disagreed
with Lua by one frame after a resume; the paused game's HUD vanished under the pause menu.

## Part 3 result (2026-11-21)
Done. A **message bus in the World** (`listen`, `unlisten`, `emit`, `invoke`): messages from an entity or game-wide,
delivered at once to listeners in the order they started listening, data as JSON so Lua and C# hear each other;
`invoke` calls a script function by name in whichever language runs it. Lua: `e:emit`, `e:on`, `noor.emit`, `noor.on`
(listeners with `:cancel()`), `e:call`. C#: `Entity.Emit`, `On(...)`, `Noor.Emit`, `Entity.Call` (rt API v15; the host
table gains `deliver` and `invoke`). Engine events are also messages from their entity (`trigger_enter`...). Listeners go
with either entity; a throwing listener is stopped alone; a loop is cut at 32 deep.

Proved: a lever's message reaching the listening door with data and sender, synchronously; game-wide messages in
listening order, a cancelled listener deaf, `e:call` with arguments and a result, a missing function an error; nothing
left after 10,000 listening entities came and went, listeners to a destroyed emitter gone; a throwing listener and a
message loop (unit); a pressure plate's `trigger_enter` heard by a door (unit, physics); a Lua lever and a door in Lua
(project, built game) and in C# (built game) showing the same HUD: messages Lua to C#, calls C# to Lua and Lua to C#
(`player.messages_lua_csharp`).

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
