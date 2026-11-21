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
| 4 ✅ | **Tweens**: `noor.tween(e, "position", to, seconds, "ease_out")` for transforms, colours, a component's numbers; sequences and parallel groups; on finish callbacks; easing functions | Every easing against its formula; a tween lands exactly on its target; tweens on a destroyed entity stop silently; built game identical |
| 5 ✅ | **Animating any property** (Godot's AnimationPlayer): a `.noortimeline` asset of keyframed tracks (a door's rotation, a light's colour, a component's field, a function call at a moment) played by an `animation_player` component; an editor Timeline panel to key and scrub | A timeline plays the same values as its keys (interpolated by hand); events fire once each; the editor keys a door opening and the game plays it |
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

## Part 4 result (2026-11-21)
Done. **Tweens** owned by the World (`runtime/tween.h`), stepped right after the systems' update on the game or real
clock: transforms (`position`, `euler`, `rotation` by slerp the short way, `scale`, `world_position`) and component
numbers, arrays and `#RRGGBB` colours; 22 easings; delay, loops (or forever), yoyo; a new tween of the same property
replaces the old; exact landing; quiet stop when the entity goes. Lua: `e:tween` with `on_done`, `t:cancel()`,
`t:active()`, `t:wait()` in sequences. C#: `Entity.Tween`, `NoorScript.Tween(..., onDone)`, `await WaitUntil(...)`; rt
API v16 (with `last_error`). `math::slerp` added.

Proved: every easing at 0 and 1 and against its formula (overshoot and dip included); a tween's midpoint and exact end,
an easing's shape; delay, a three-play yoyo, forever until replaced, cancelling, a destroyed entity, bad requests with
reasons; a light's intensity, a colour, a number array, rotation the short way and Euler the long way; paused,
real-time and slow-motion tweens (unit); `on_done` and `t:wait()` in Lua (unit); a door chained by on_done (rise,
bounce, turn red) and a kinematic yoyo platform, identical in Lua in a project and Lua and C# in a built game
(`player.tween_lua_csharp`).

Not as planned: no separate "sequence and parallel group" objects: tweens run side by side by default, and one after
another with `on_done`, `delay` or `t:wait()` in a sequence. Found on the way: a 0.5 s tween finished a frame late
(the same thirty-sixtieths sum as part 2's timers), fixed the same way.

## Part 5 result (2026-11-21)
Done. **`.noortimeline`** assets ([Timelines](../animation/TIMELINES.md); `asset/timeline_asset.h`): value tracks on the
entity or its children by path (anything a tween moves, an easing per key), call tracks (a script function, Lua or C#)
and emit tracks (a message). The **`animation_player`** component and `TimelineSystem` (`game/timeline.h`): named
timelines, autoplay, speed, looping, keys fired once as play passes them, `timeline_end` when one ends, on the game
clock. Scripts: `e:play_timeline`, `e:stop_timeline`, `e:timeline()`; C# `PlayTimeline`, `StopTimeline`, `Timeline`
(rt API v17). **Editor:** `noor_doc_timeline` (editor API v32: list, info, new, set, key from the scene, event,
remove_key, preview) and the **Timeline** tab (a track view with a ruler and keys, a scrub that previews in the viewport
without touching the scene or its undo history, Play and Stop, Key position / rotation / scale / property, Add call,
Delete key). The tweens' property code is shared (`read_property`, `write_property`).

Proved: files parsed, saved and read back the same, keys sorted, bad ones refused; keys around a time (unit); a door
timeline with a hinge, a light with an easing, call keys and an emit, once each, ending with `timeline_end`; looping
fires every pass; play, stop and ask from scripts; pausing freezes it (unit); the editor API: new (file and component),
keys read from the scene, events, info, a preview that shows a moment and leaves the scene and its undo as they were,
removing keys, errors with reasons (API test); the smoke test makes a door's timeline in the panel, keys the hinge,
scrubs to 0.5 s (the engine shows 45 degrees, the scene keeps 90, no undo step) and plays the game: the hinge opens; Lua
in a project, Lua and C# in a built game end on exactly the same state and events (`player.timeline_lua_csharp`).

Exceptions: key edits are saved to the file at once, not undo steps; no key dragging, curve editor or blending between
timelines; scrubbing doesn't fire call or emit keys.

## Part 6: how it's split (2026-11-21)
The spike chose RmlUi ([ADR-041](../decisions/ADR-041-game-ui.md), accepted). Part 6 is too big for one step:

| Step | What | Proves |
|---|---|---|
| 6a | RmlUi and FreeType as pinned dependencies; a `ui` module wrapping them; a UI pass in the Vulkan renderer (textured, scissored, premultiplied geometry and the textures RmlUi makes); `.noorui` documents shown in a game | A document drawn in the player matches its golden; the renderer's UI pass under Vulkan validation; a project without UI pays nothing (yardsticks) |
| 6b | Scripts: open and close documents, events from elements (`click`, `change`, `submit`), data from scripts into the page, Lua and C# | A menu that pauses and resumes the game, the same from Lua and C# in a built game |
| 6c | Beautiful: gradients, shadows, blur (layers and filters), transforms in the renderer; built games carry the credits and licence files | Goldens of each effect |
| 6d | The editor's UI builder: `.noorui` files edited with a live preview | The smoke test builds a menu |
| 6e | Gamepads and rebinding (the plan's 6b) | As planned |

## Part 6a result (2026-11-21)
Done. **RmlUi 6.3 and FreeType 2.14.3** are pinned dependencies (`cmake/NoorDependencies.cmake`, SHA-256 checked, static;
`third_party/manifest.json`); the spike uses the same ones. The **`ui` module** (`engine/ui`, `noor::ui::Ui`) wraps them:
documents opened from inside the project only, the built-in font always loaded, a default style sheet given to each
document as it loads (blocks, the font, a light text colour), `dp` = 1/720 of the picture's height, the viewport's
offset, the mouse, textures through a `TextureHost` (the renderer's, or a test's). The **renderer's UI pass**
(`vulkan_ui.cpp`, `ui.hlsl`): indexed triangles with a texture, translation and scissor per draw, CSS's premultiplied
sRGB colours turned into linear light, textures from the bindless table with a new clamping sampler (binding 4), drawn
after the HUD; its pipeline is made the first time a frame has UI. **In games:** a `ui_document` component
(`{"document": "UI/x.noorui", "visible": true}`) opens a document in noor_player and built games, laid out in the
display's picture, on the world's real-time clock. The built-in font moved to its own small target (`engine/fonts`) so
the HUD and the UI share one copy. Built games carry `RmlUi-LICENSE.txt`, `FreeType-FTL.txt` and `CREDITS.txt` (moved
here from 6c: the player links them from now on). Docs: [Game UI](../ui/GAME_UI.md).

Proved: a document becomes draws with the font's textures, moved by the viewport, clipped boxes as clip rectangles in
screen pixels that follow the picture's height, hidden and closed documents draw nothing, the default style sheet's
blocks, files outside the project refused, missing ones with a reason, one `Ui` at a time and its textures all given
back (unit, no GPU); a pause menu and a settings panel (rounded boxes, three text sizes, a border, an image, a clipped
list) over the Daylight scene match their 640 x 360 golden under Vulkan validation with no errors or warnings
(`render.golden_UI_Menu`); a game without UI doesn't start it: the yardsticks' memory, threads, CPU and GPU are
unchanged, and side by side with the player before 6a (on a busy PC) the first frame and the GPU time are the same.

Exceptions: the player is 4.2 MB bigger (16.5 to 20.6 MB) whether a game uses UI or not; documents don't show in the
editor's Play yet (6d); no scripts, keyboard or gamepad yet (6b, 6e); rounded clipping, gradients, shadows and filters
need the renderer's clip masks and layers (6c); destroying a UI texture waits for the GPU (as every texture does today).

## Part 6b result (2026-11-21)
Done. **Scripts and UI documents.** One operation, `runtime::api::ui(world, caller, {"op": ...})`, does everything for
Lua and C# alike: open (the document belongs to the caller's entity and closes with it), find, show, hide, close,
visible, and by element id set_text (escaped), set_rml, set_class, set_attribute, set_style, value, set_value, focus,
click. The runtime knows UI only through `runtime::UiDocuments` (`ui_documents.h`); `noor::ui::WorldUi` implements it,
starts RmlUi with the first document (a component's or a script's), and sends RmlUi's events (click, change, submit,
focus, blur, mouseover, mouseout; hover ones only when listened to) to the World as messages from each document's
emitter id: "click" and "click#resume", with {type, element, value}, queued and delivered at the start of the next
frame (`World::queue_message`). **Lua:** `noor.ui.open`, `noor.ui.find`, document methods, `d:on(event [, element],
fn)`. **C#** (rt API v18, one `ui` entry carrying the same JSON requests): `Ui.Open(path, owner)`, `Ui.Find`,
`UiDocument` with the same methods, `NoorScript.On(document, type, element, handler)`. **Input:** the player gives the
UI the mouse (not while captured), key presses with their auto-repeats, typed text (SDL text input, turned on while a
text box has focus) and the Windows clipboard; documents take the keyboard when opened or shown, so Tab, arrows (with
`nav`) and Enter work. The platform gained Delete, Home, End, Page Up and Page Down, `InputState::key_presses` and
`text`, `set_text_input`, the clipboard.

Proved: elements by id (text escaped, markup, classes, attributes, styles), control values (a range a number, a
checkbox true / false, a text box a string), clicks naming the element with an id, changes with their value, Tab
moving focus and Enter clicking, typing and Backspace in a text box (accents too), copy and paste through the host's
clipboard, missing elements refused (unit); documents in a World: a component's opened by `WorldUi`, a script's request
answered, mistakes named, events delivered as "click" and "click#go" the frame after, a document closed with its owner
and the component's kept, RmlUi not started until a document opens (unit); a menu script that pauses the game, fills
in the menu, reads it back, clicks Resume (which resumes the game) and keeps a HUD page up to date ends on exactly the
same line from Lua in the project and Lua and C# in a built game (`player.ui_lua_csharp`). `tests/data/ui_project`'s
`Scenes/Try` is the same kind of menu to play with by hand.

Exceptions: binding a page to a script's data (RmlUi's data models) isn't there yet: scripts set values; the editor's
Play doesn't show documents (6d); `ui_document` components on entities spawned later aren't opened (scripts open
documents instead); the mouse also reaches the game while over a menu (pausing makes that harmless); the player's mouse
and keyboard mapping is checked by hand, not by a test.

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
