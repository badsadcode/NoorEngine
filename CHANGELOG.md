# Changelog

Technical, honest, newest first. "Works" means tested; anything else says what it is.

## [Unreleased] — Milestone 5 in progress (PBR lighting, HDRI and materials)

### 2026-11-17 — Milestone 5, part 1: the frame foundation
**Added**
- The scene is lit into a **16-bit float HDR image** in physical units, then **exposed and
  tonemapped** to the screen (ADR-012). Editor gizmos and the HUD are drawn after tonemapping in
  their own display colours. `vulkan_frame.cpp`, `tonemap.hlsl`.
- **PBR shading core**: GGX / height-correlated Smith / Schlick specular, Lambert diffuse; ambient
  from a ground-to-sky hemisphere with a specular part. Surfaces are rough dielectrics until
  materials (part 3). Documented in `docs/rendering/PBR.md`.
- `render::Lighting`: sun direction, colour and illuminance (lux), sky colour and luminance
  (nits), exposure (EV100, default 14), tonemapper (Khronos PBR Neutral default, ACES fit, none),
  sky or fixed background. `FrameDesc::lighting`.
- A **`light` component** (`"type": "directional"`, `color`, `illuminance`, and for now
  `sky_color`, `sky_luminance`): the sun shines along its entity's forward. `camera` gains
  `ev100` and `tonemapper`. **Create ▸ Sun (directional light)** in the editor.
- Vertices gain **UVs and tangents** (`render::Vertex`, `asset::MeshVertex`, 60 bytes): GLB
  `TEXCOORD_0` and `TANGENT`, tangents computed from UVs when missing; the built-in box, sphere and
  capsule have UVs.
- **GPU time per pass** (`FrameStats::passes`: Scene, Tonemap, Gizmos, HUD); in noor_player's
  `--report` as `gpu_passes`.
- **Golden-image render tests**: `noor_image_compare` (shrinks a screenshot, compares it with a
  PNG reference with a tolerance, writes a diff picture) and `tests/data/render_scenes`
  (Daylight, Sunset). `NOOR_UPDATE_GOLDEN=1` refreshes references. stb_image and stb_image_write
  (public domain/MIT, pinned) for reading and writing images.

**Changed**
- Scenes look different: lit by a real sun and sky instead of a fixed fake light. Very dark
  colours now look very dark in sunshine (that's physically right; see PBR.md).
- The player test checks that the centre pixel is the red box by colour, not brightness.

**Tests:** 3 new render tests (two golden images and one that must fail on a changed picture).
noor_diag's exact-purple background check still passes through the new tonemap pass.

**Not yet:** materials and textures (parts 2–3), shadows (4), HDRI skies (5), MSAA and the
passes inspector in the editor (7), AgX (licence check), depth pre-pass (when measured).

### 2026-11-17 — Milestone 4, part 8: NoorFPS-lite and the Milestone 4 review
**Added**
- **NoorFPS-lite** (`samples/NoorFPS-lite`): a first-person target shooter made only with the
  public script API. Walk, sprint, jump, look, shoot gold targets against a one-minute clock,
  footsteps, shot/hit/round-end sounds (synthesised for the sample), physics crates to knock about,
  a HUD (score, accuracy, clock, hit marker), pause on Esc, a self-playing demo before the first
  click. Written in C# (`Scenes/Main.noorscene`) and as a Lua twin (`Scenes/Main_Lua.noorscene`).
- `noor_player --no-input`: the game gets no keyboard or mouse (tests).
- The player's `--report` includes the last frame's HUD text (`hud_text`), so tests can read a
  game's score.
- **Milestone 4 review** (`docs/milestones/M4_REVIEW.md`): gate passed, exceptions listed.

**Changed (breaking for C# scripts)**
- C# `Entity` is now a class (a handle, equal by id) instead of a struct: `Entity.Position = v`
  and `hit.Entity.Position = v` used to fail to compile (CS1612). `Noor.Find`, `Parent` and
  `Child` still return `Entity?`; drop any `.Value` on them.

**Fixed**
- `raycast(…, ignore)` didn't skip a character's controller body, so a shot from the player's eyes
  hit the player at distance 0.
- A real click on a built game's window could change an automated test's result: the player and
  sample tests now run with `--no-input`.

**Tests:** `cli.build_sample_fps` builds the sample with noor_cli; `sample.fps_csharp` and
`sample.fps_lua` run its demo for 15 s with no input and read the score off the HUD (≥ 10; 24 in
practice). A unit test for raycasts ignoring a character.

**Not yet:** see the review's exceptions: prefab spawning, timers, `require`, gamepad, app-local
runtimes, NoorCode completion and debugging, rays vs triggers.

### 2026-11-16 — Milestone 4, part 7b: NoorCode MVP
**Added**
- **NoorCode**, the built-in script editor: scripts open as tabs next to the viewport (Inspector
  ▸ Open, or the links in the Console). Built on **AvaloniaEdit** 12.0.0 (MIT, NuGet; editor only).
  Lua/Luau and C# highlighting (our own `.xshd` files in the NoorTheme colours), line numbers,
  find (Ctrl+F), undo, spaces for tabs.
- Ctrl+S saves the script whose tab has the keyboard; a running game reloads it as before. Play and
  Build Game save open scripts first. Closing a tab, the project or the editor asks about unsaved
  scripts. A file changed outside reloads in its tab; with unsaved edits too, a bar asks which to keep.
- Errors that name a script line (Lua errors, C# compile errors, C# exceptions) are marked on the
  line in red and listed under the editor (click: go to the line). In the Console they get a
  `File:line ↗` link that opens the script there (`ScriptLocation`).
- Preferences ▸ Scripts: "Open scripts in NoorCode" (on by default); the external editor stays as
  NoorCode's "Open in …" button, and is used directly when NoorCode is off.

**Fixed**
- C# compiler messages in the PC's language (Polish here) were garbled ("Oczekiwano Ļ›rednika"):
  the editor and noor_cli now read `dotnet build` output as UTF-8.

**Tests:** 15 new editor smoke checks (133 total): highlighting loads, error messages resolve to
script lines, open at a line, typed keys go to the text and not the editor's shortcuts, Ctrl+S,
Play saving first, a compile error marked on line 7 and linked from the Console, outside changes,
closing. Looked at in a screenshot.

**Not yet:** code completion, error checking while typing, a debugger (spec 7 is the full IDE:
these need language services, Roslyn for C# and a Luau analyser); open tabs aren't remembered
between sessions.

### 2026-11-16 — Milestone 4, part 7a: the HUD
**Added**
- Scripts draw a HUD every frame: Lua `noor.ui.text(text, x, y [, {size, color, anchor}])`,
  `noor.ui.rect(x, y, w, h [, {color, anchor}])`; C# `Ui.Text(...)`, `Ui.Rect(...)`.
  `noor_rt_api` **v3**. UI units (the screen is 720 tall), nine anchors, `#RRGGBB[AA]` colors;
  wrong colors/anchors are script errors naming the problem.
- `runtime::UiCommand` / `World::ui()` (cleared each frame), `api::parse_color` / `parse_anchor`.
- Renderer: a 2D **overlay** pass over the 3D scene (`OverlayVertex`, `set_overlay_atlas`;
  `vulkan_overlay.cpp`, `overlay.hlsl`): alpha blending, solid shapes, signed-distance-field text
  (four samples per pixel, so small text keeps thin strokes).
- `game::Hud`: bakes the built-in font's distance fields with **stb_truetype** (public domain/MIT,
  pinned) at start (~1024×512 atlas), lays out text (UTF-8, line breaks, kerning, anchors) and
  rectangles. Font: **Atkinson Hyperlegible** (SIL OFL 1.1, pinned, embedded in the engine);
  Latin, Latin-1, Latin Extended-A (Polish…) and common symbols. Builds carry its licence in
  `licenses/`.
- The HUD shows in noor_player and in the editor's Play. The first-person player template draws a
  crosshair while looking, and a "Click to look around" hint until then.

**Tests:** 5 HUD unit tests (colors, anchors, placement maths, measuring, font coverage incl.
Polish, vertices per glyph, Lua `noor.ui` incl. bad options); the player tests check the HUD's
vertex count (48; 60 with a C# label) and that the pixel at UI (40, 40) is the script's green square.
Small text was checked by eye in screenshots (14 units: fixed thin strokes by supersampling).

**Not yet:** images/icons on the HUD, other fonts, clickable buttons, text wrapping.

### 2026-11-16 — Milestone 4, part 6b: C# scripts in built games
**Added**
- `scripting::start_dotnet`: noor_player starts .NET with hostfxr (the .NET runtime installed on
  the PC; no SDK) and loads the game's compiled scripts through `ScriptHost.Boot`. ADR-008 notes.
- `noor_cli build` compiles `Scripts/**/*.cs` (Release, with line numbers) into `managed/` with
  NoorEngine.Scripting, before writing anything; compile errors read `Scripts/X.cs:7: CS1002: ...`.
- The engine build now compiles NoorEngine.Scripting (`noor_scripting_managed`, needs the .NET SDK;
  without it CMake says games can't have C#). CI installs .NET before configuring.
- NoorEditor's File ▸ Build Game packs C# scripts too.
- NoorEditor: **Edit ▸ Preferences ▸ Scripts ▸ Code editor** (asked for by Tom): Windows default, an
  editor found on the PC (VS Code opens the Scripts folder at the line, Visual Studio, Rider,
  Notepad++, Notepad), or any program with `{file}` / `{line}` / `{folder}` arguments. The
  Inspector's Open button uses it. The dialog's layout hasn't been looked at in a screenshot yet.

**Fixed**
- **Play at the screen's rate** (Tom: walking in Play "doesn't feel like 60 FPS", flying is fine):
  the editor ticked the game from a 16 ms UI timer, which Windows rounds to ~15.6 ms steps and the
  busy UI thread delays: **34 updates/s** measured (each costing ~2 ms). Now the viewport's render
  thread runs one game frame before every frame it draws, like the fly camera
  (`noor_viewport_set_play`, editor API **v9**; the document locks its running world for the UI
  thread's panel reads; a script's mouse-capture request reaches the UI thread as a viewport event).
  Measured: 58–60 updates/s; the editor smoke test now checks it (118 checks).
  Regression found by Tom right after: the player couldn't move. The viewport's "has focus" used
  `GetFocus()`, which only answers for the calling thread's windows, so on the render thread the
  game got no keys (the smoke test's test-pointer mode counts as focused, so it passed). Focus is
  now tracked from WM_SETFOCUS/WM_KILLFOCUS; a unit test reads it from another thread.
- **Smooth motion** (Tom: "not as smooth as in other games"): physics steps at a fixed 60 Hz, and
  even on a 60 Hz screen frame times wobble, so some frames got no step and some two: walking
  stuttered slightly. Physics now tells the World where to *draw* what it moves, blended between its
  last two steps (`World::set_render_pose`, `fixed_alpha`); the editor's Play and the player draw
  that, children (the player's camera) follow, scripts and the simulation keep the real transforms.
  Costs one physics step (1/60 s) of visual lag, as in other engines. Test: with frame times wobbling
  ±1.5 ms the simulated position stalls on some frames while the drawn one moves within 12 mm of
  speed × dt every frame. Not yet checked by hand.
- Built games: the view jerked on the click that captures the mouse (entering relative mode warps
  the hidden cursor to the window centre, which arrived as movement); switching to another window
  now releases the mouse like Esc. Found by Tom; not yet re-checked by hand.
- The copies of noor_player next to noor_cli (from 6a) only refreshed when noor_cli itself relinked;
  now a separate step refreshes them every build.
- Scripts loaded by a host that isn't the editor couldn't find NoorEngine.Scripting (hostfxr loads it
  in its own context); the scripts' context now resolves it to the host's copy.

**Tests:** `cli.build_csharp`, `cli.build_csharp_error` (CI) and `player.built_csharp_game` (GPU):
a box lifted by a C# script in a built game rises exactly 2 m in 120 frames. Editor smoke: the
built game includes the compiled C# (114 checks). The game the editor smoke test builds (player,
physics, Lua, C#, sound) also ran 180 frames without errors when started by hand.

**Not yet:** self-contained builds (bundling .NET); `noor_player <project>` doesn't compile C#.

### 2026-11-16 — Milestone 4, part 6a: games that run without the editor
**Added**
- `game` module (ADR-028): `make_world()` (the world with every engine system), `collect_view()`
  (models, primitive shapes, the game camera from a running scene), `SceneRenderer`, primitive
  meshes. The editor's Play and its render snapshots now go through it, so editor and player run
  the same code.
- `noor_player`: runs a game in its own window (`noor_player` next to a built game, or
  `noor_player <project folder>`). Keyboard and mouse into the game's input actions, mouse capture
  on the script's request (Esc releases), sound, physics, Lua. Test options `--frames`, `--report`,
  `--screenshot`, `--no-audio`.
- `noor_cli build <project> <out> [--scene]`: validates, then writes `<Name>.exe` (the player),
  `game.json` and `game/` (the project's files without derived data). Rebuilds replace an earlier
  build; non-empty folders and folders inside the project are refused.
- NoorEditor: **File ▸ Build Game…** (Ctrl+B): saves if you like, asks for a folder, builds starting
  with the open scene, then offers **Play it** / **Show folder**. Output goes to the Console.
- `asset::Project::default_start_scene()`.

**Tests:** `cli.build` (headless, CI); `player.project` and `player.built_game` (GPU, local): 120
fixed frames of `tests/data/player_project` from a project and from a build; the ball must rest on
the floor, a Lua script must have lifted a box exactly 2 m, and the centre pixel must be the red box
seen through the scene's camera. Editor smoke test: File ▸ Build Game (113 checks).

**Not yet:** C# scripts in builds (done in 6b, above). Builds need the Visual C++
2015–2022 runtime on the PC; Debug tools make games that only run where Visual Studio is installed
(noor_cli warns). No packing/cooking: a build's content is plain project files, and the registry
is rescanned (assets hashed) at every start. No game icon, no fullscreen option yet.

### 2026-11-15 — Milestone 4, part 5: sound
**Added**
- `audio` module (ADR-011): **miniaudio** (pinned single header, MIT-0/public domain) as
  `audio::AudioSystem`. `audio_source` (clip, volume, pitch, loop, spatial, min/max distance, bus,
  play on start, stream), `audio_listener` (else the first camera hears), buses master/music/sfx/
  ui/voice, inverse-distance 3D attenuation, one-shots that clean up after themselves.
- `.wav`, `.flac`, `.mp3` files are `audio` assets (registry importer "audio").
- Without a sound device (CI, tests, `NOOR_AUDIO_DEVICE=none`) the mixer still runs, into memory,
  so the same code paths are tested everywhere.
- Scripts: Lua `e:play_sound()`, `e:stop_sound()`, `e:is_playing()`, `noor.play_sound(clip [,
  position [, volume [, bus]]])`, `noor.set_volume(bus, v)`; C# `Entity.PlaySound()/StopSound()/
  IsPlaying`, `Noor.PlaySound(...)`, `Noor.SetVolume(...)`. `noor_rt_api` **v2**.
- `System::starts_late()`: script systems start entities after engine systems, so `on_start` can
  use the entity's sound (and anything else engine systems set up).
- Editor API **v8**: `noor_doc_set_audio_device`. NoorEditor: audio assets show as **Sound** (♪);
  **Add to scene** on one makes an emitter; **Add component ▸ Sound**; the first-person player's
  camera has an `audio_listener`.

**Tests:** 4 audio tests (silence when idle; play/loop/stop; near is louder than far and a muted
bus is silent, measured from the mixed samples; one-shots clean up, a missing clip is an error,
not a crash; a Lua script driving it all). The editor smoke test writes a WAV, adds it to the
scene, and plays it with a C# script (looping emitter + one-shot), silently.

**Fixed**
- Create ▸ … and Add to scene did nothing while playing (only a Console warning); they now ask to
  stop the game first.
- Sound stopped for good after Bluetooth earbuds went into their case and came back (found by Tom).
  miniaudio follows default-device changes but ignores failures, and Bluetooth reconnects in stages.
  NoorEngine now owns the device: one that stops pulling audio for 1 s is rebuilt, with retries
  every 1-5 s while there's none (sounds keep playing silently meanwhile). Also: a game started
  with no device picks one up when it appears. Tested by stopping the device as Windows would
  (local only: CI has no sound card, so that test skips there), and by Tom with real Bluetooth
  earbuds: into the case and out again during Play, the music came back.
- **Tests that never ran.** CMake splits lists on `;`, so 15 test cases with a `;` in their name
  became ctest entries that matched nothing and "passed". Renamed; every ctest entry that runs zero
  test cases now fails. Of the 15, one had a wrong expectation (a stopped sound fades out for a few
  ms instead of cutting off with a click) and was corrected; the rest passed as they were. 138 real
  tests now, instead of 152 entries.

**Not yet:** pause, fades, voice limits, device switching, `.ogg`, occlusion/reverb. Automated tests run
without a device; speaker output was checked by ear by Tom (music box emitter, walking around it).

### 2026-11-15 — Milestone 4, part 4: C# scripts
**Added**
- `noor_rt_api.h`: the scripting ABI as two tables of C function pointers, `noor_rt_api` (engine
  functions scripts call; the Lua set) and `noor_rt_host` (the managed host). ADR-008 updated.
- `scripting::CSharpScriptSystem`: the engine half; same hook order and error handling as Lua;
  reports once (not every frame) when no host is available.
- `engine/scripting/dotnet/NoorEngine.Scripting`: `NoorScript`, `Entity`, `Noor`, `Input`,
  `RaycastHit`, and `ScriptHost` (loads compiled scripts from memory into a collectible
  `AssemblyLoadContext`, unloads on Stop; exceptions never cross into native code and are reported
  with type, message and file:line).
- Editor API **v7**: `noor_rt_get_api`, `noor_doc_set_script_host`. NoorEditor: Play compiles C#
  scripts (`Scripts/Scripts.csproj` generated for IDEs; `dotnet build` ~1.2 s), compile errors go
  to the Console as `Scripts/X.cs:7: CS1002: ...` and Play doesn't start; **Add component ▸ C#
  script…** with a template.
- `docs/scripting/CSHARP_API.md`.

**Tests:** 3 engine-half tests with a fake host (hook order, callbacks into the world, input,
exceptions, missing class, missing host, id round trip); the editor smoke test writes a real C#
script, plays it (87° in ~1 s at 90°/s, `Entity.Set`, `Noor.Find`, `Noor.Log`), checks a compile
error (file:line, no play), an exception (line, game continues) and reload after editing.
Provoked errors are counted so the "zero errors" budget stays meaningful.

### 2026-11-15 — Milestone 4, part 3b: a walking first-person player
**Added**
- `character` component on Jolt `CharacterVirtual`: capsule with feet at the entity origin, slides
  along walls, walks up steps (`step_height`) and slopes (`max_slope`), sticks to the floor going
  down, jumps only from the ground; an inner body so raycasts and triggers see the player.
  `character_move` (per frame), `character_jump`, `is_grounded` in C++ and Lua.
- `camera` component: while playing, the viewport looks through the first camera entity
  (picking, gizmos and `world_to_screen` use the same camera).
- Mouse capture for look: scripts ask (`noor.input.capture_mouse`), the host captures only a
  focused game view (cursor hidden and clipped, raw mouse movement as `look_x`/`look_y`); Escape,
  focus loss and Stop always release it. Tests never take the real mouse (test pointer mode).
  Editor API: `noor_viewport_set_mouse_capture`, `noor_viewport_mouse_captured`.
- `e:child(name)` in the script API.
- NoorEditor: **Create ▸ Player (first person)** (player with character + script, a capsule body and
  an eye-height camera; one undo step) and the `Scripts/PlayerController.luau` template (WASD, Shift
  sprint, Space jump, mouse look after a click, Esc to release).

**Tests:** 5 character tests (standing/walking/stopping, jump only from the ground, 0.3 m kerb up and
2 m wall stop, raycast and trigger see the player, a Lua script steering and asking for the mouse);
the editor smoke test creates the player, plays, checks the view is the player's camera, walks
(3.1 m), jumps (1.23 m peak; 1.27 predicted), captures and releases the mouse (91 checks).

**Fixed (tests)**
- Under ctest the editor is launched from a background process and Windows may not let it take
  the foreground, so keyboard focus (and held keys) came and went. The viewport's test mode now
  counts as focused, and the editor asks the engine (`noor_viewport_has_focus`) instead of Windows
  whether the viewport has focus. 139/139 Debug, 133/133 Release without GPU.

### 2026-11-15 — Milestone 4, part 3a: physics (Jolt)
**Added**
- Jolt Physics 5.6.0 (MIT) pinned, built for a portable SSE4.2 baseline (no AVX2), without its GPU
  compute, debug renderer or profiler.
- `engine/physics`: `PhysicsSystem`, a runtime system. `collider` (box, sphere, capsule, mesh;
  offset; trigger) and `rigidbody` (dynamic, kinematic, static; mass, friction, restitution,
  damping, gravity scale) components; sizes follow the entity's world scale; dynamic bodies move
  their entities (also as children of moved parents), kinematic bodies follow them; fixed 60 Hz
  step after the scripts' fixed update; raycasts; velocity, impulses, forces; collision and trigger
  enter/exit events. Mesh colliders use the model's own triangles, or a referenced collision mesh
  (Noor Interchange collision nodes).
- Sleeping bodies don't fake "exit" events: Jolt drops a resting body's contacts when it sleeps;
  those pairs are parked and only reported if the body wakes up and really leaves.
- `runtime::World` events (cleared each frame); `api::world_matrix`, `world_rotation`,
  `set_world_pose`.
- Lua: `noor.raycast`, `velocity`/`set_velocity`/`add_impulse`/`add_force`, hooks
  `on_collision_enter/exit`, `on_trigger_enter/exit`.
- Primitive shapes (`primitive` component: box, sphere, capsule, plane, color) rendered and pickable
  in the viewport. Editor API **v6**: `create` command (one undo step). NoorEditor: **Create** menu
  (empty, box, sphere, capsule, floor, physics crate, physics ball), **Add component ▸ Physics /
  Shape** presets.

**Fixed**
- Keys typed into the viewport could reach whichever editor control last had focus: Space "clicked"
  a focused button (found when a play-mode test's Space press sometimes did nothing). Viewport keys
  are now never delivered to editor controls.

**Tests:** 8 physics tests (falling and resting, one enter and no fake exit when asleep, wake-up
exit/enter, raycasts, bounce, impulse, trigger, kinematic pushing, mesh collider, refused dynamic
mesh, parented bodies, Lua hooks/raycast/impulse); the editor smoke test builds a floor and crate
from the Create menu and drops a ball on the BadBuilding roof through a GLB mesh collider (81 checks).
Its play-mode input checks now test direction, not speed: under load the editor's play timer
advances less game time (speed itself is unit-tested). 134/134 Debug, 128/128 Release without GPU.

### 2026-11-15 — Milestone 4, part 2: Lua scripts (Luau)
**Decided:** ADR-009 accepted: Luau (project owner confirmed). Pinned Luau 0.745 (MIT).

**Added**
- `runtime::api`: the scripting API in one place (entities, local/world transforms, Euler in the
  editor's convention, directions, component data, input actions/axes, time), for Lua now and C#
  in part 4.
- `engine/scripting`: `LuaScriptSystem`, a runtime system. `script` component
  (`{"file": "Scripts/X.luau", "params": {...}}`); hooks `on_start`, `on_update`,
  `on_fixed_update`, `on_late_update`, `on_destroy`; one instance (`self`) per entity with
  `self.entity` and `self.params`; Luau vectors for positions/directions; `noor.log` with file and
  line. Sandbox: Luau's safe libraries, read-only globals and built-ins, per-file globals; a hook
  that runs over 0.25 s is stopped; an erroring script is reported (file, line, stack) and
  switched off for that entity while the game goes on.
- NoorEditor: **Add component ▸ Lua script…** (creates `Scripts/<Name>.luau` from a template),
  the script card shows the file with **Open** and its params as editable fields (**+ Param**).
- `docs/scripting/LUA_API.md`.
- Tests: 8 Lua tests (hooks/params/vectors, input/find/rotation/directions, runtime and syntax
  errors with file:line, sandbox, endless-loop budget, destroy) and an Euler round trip; the editor
  smoke test creates a script from the Inspector, edits it and drives the building with D and
  Space in play mode (74 checks).

### 2026-11-15 — Milestone 4, part 1: the runtime module and game input
**Decided:** ADR-027 (runtime, frame order, one scripting API for C# and Lua), ADR-008 (.NET 10
CoreCLR hosting), ADR-010 (Jolt physics), ADR-011 (miniaudio), ADR-009 proposed (Luau recommended;
one question open). Plan: `docs/milestones/M4_PLAN.md` (8 parts, risks).

**Added**
- `engine/runtime`: `World` (systems; frame order start → update → fixed 60 Hz steps (max 5) →
  late update → destroy; dt capped at 0.1 s; spawn/destroy with whole subtrees), `Input` with
  actions and axes (`Settings/input.json`, defaults: move_x/move_y, jump, sprint, crouch, fire,
  aim, interact, pause; look_x/look_y from the mouse), built-in behaviours moved here
  (`rotator`, new `input_mover`).
- Editor API **v5**: play mode runs a `World`; `noor_doc_tick` takes the game-view viewport, whose
  keyboard and mouse are the game's input while it has focus (not while the right button flies
  the editor camera). While playing, keys typed into the viewport go to the game; only Ctrl
  shortcuts reach the editor.
- `noor_viewport_test_pointer`: tests inject the pointer instead of posting mouse messages.

**Fixed**
- The editor smoke test's clicks and drags depended on where the real cursor was: Windows sends a
  mouse-move with the real cursor position after `SetCapture`, so a cursor resting over the editor
  bent every drag. Found when the test failed identically twice in a row.

**Tests:** 7 runtime tests (frame order, fixed step, lifecycle, input edges/axes, input.json,
behaviours); the editor smoke test plays with keys pressed in the viewport (66 checks).
115/115 Debug, 109/109 Release without GPU.

## Milestone 3 complete

### 2026-11-15 — Milestone 3 complete: play mode, live reload, gate review
**Added**
- ADR-026 (play mode). Editor API **v4**: `noor_doc_play` / `stop` / `tick` (a runtime copy of
  the expanded scene; edits, undo and scene switches refused while playing; Stop restores) and
  `noor_doc_poll_files` (watched asset files: changed models get a new version in the render
  snapshot and viewports reload them; changed prefabs are re-read).
- `scene::update_behaviours`: the first built-in behaviour, `rotator` (axis, degrees per second).
- NoorEditor: **▶ Play / ■ Stop** (Ctrl+P), running values shown in the panels a few times per
  second, gizmos hidden and Inspector read-only while playing; asset files polled twice a second;
  the hierarchy only rebuilds when its structure changes.
- `docs/milestones/M3_REVIEW.md`: gate review (complete with 6 exceptions). HiDPI: checked by
  hand at 150 % (the editor followed a live scaling change and looked right); the automated test
  has run at 100 % only.
- Tests: rotator unit test; document tests for play/stop, live prefab reload, undo edge cases
  (after save, redo branch, transactions, scene switch); the editor smoke test plays, live-reloads
  a model, edits through a floating Inspector and asserts swapchain = control × render scaling
  (61 checks). 108/108 Debug, 102/102 Release without GPU.

### 2026-11-15 — Milestone 3, part 3: scale gizmo, snapping, preferences, component editing
**Added**
- Editor API **v3**: `NOOR_GIZMO_SCALE` (handles along the entity's own axes + a uniform centre
  cube) and `noor_viewport_set_options` (snap on/off and steps, fly speed, look sensitivity,
  gizmo size). Snapping: moves land on a world grid, rotations on angle steps, scales on scale
  steps; holding Ctrl while dragging inverts snapping. Each drag's result is computed once per
  frame and used both for the preview and the final command, so what you see is what you get.
- NoorEditor: **R** = scale tool; **Snap** toggle in the viewport toolbar; **Edit ▸ Preferences**
  (camera and gizmo settings, saved per user); the **Inspector edits components**: numbers, text,
  true/false and short number lists, add/remove fields, add (marker or custom) and remove
  components, each change one undo step. Engine-managed components (model, mesh, prefab) stay
  read-only.
- Tests: snap and scale-handle unit tests (one caught a divide-by-zero in the centre-cube pick);
  the editor smoke test drags the scale handle (2.01x), makes a snapped move (lands on x = 15),
  saves preferences and edits/removes/undoes a component. 101/101 Debug.

### 2026-11-15 — Fly mode
Requested after the first hands-on try of the gizmos on the street demo ("no fly mode?").
- Viewport camera: **hold the right button** to look around and fly (WASD, Q/E down/up, Shift x4,
  wheel changes the speed while flying). Orbit moved to **Alt + left-drag**; middle-drag pans and
  the wheel zooms as before. Fly keys are not forwarded as editor shortcuts while flying.
- `editor::EditorCamera` (orbit/look/pan/zoom/fly) moved out of the render loop into the
  unit-tested viewport tools; the native child window tracks fly keys and Alt.
- Tests: 2 camera unit tests; the editor smoke test flies (right button + W through the viewport's
  own window: 4.8 m in 0.6 s) and mouse-looks (40 checks). 99/99 Debug.

### 2026-11-15 — Milestone 3, part 2: click to select, gizmos, layout memory
**Added**
- Editor API **v2** (ADR-025 addendum): `noor_viewport_set_selection` (highlight),
  `noor_viewport_set_gizmo` (move/rotate gizmo on one entity), `noor_viewport_poll_events`
  (`click`, `transform`), `noor_viewport_world_to_screen`. `set_transform` accepts an undo `label`.
- Viewport: left click picks the nearest mesh or marker (ray vs. triangles, AABB first); selected
  objects drawn with a purple tint; **move gizmo** (X/Y/Z arrows + XZ plane square) and **rotate
  gizmo** (X/Y/Z rings, around the entity's origin), world-aligned, constant size on screen, hover
  highlight; drags previewed live and reported on release.
- Renderer: per-draw `tint` and `overlay` draws (drawn after the scene with depth cleared). Push
  constants stay at 128 bytes (the world 3x3 is sent as three columns).
- Math: `inverse(Mat4)`, `transform_vector`, `conjugate`/`normalize_quat`/`rotate` for quaternions.
- Render snapshots carry every expanded entity (world matrix, world rotation, local transform,
  editable flag) and marker ids.
- NoorEditor: Select/Move/Rotate tools (Q/W/E, toolbar, Edit menu); a click selects the whole
  object first, clicking again inside the selection goes one level deeper; gizmo results become
  one undo step each ("Move"/"Rotate"). Panel sizes and active tabs are remembered;
  Window ▸ Reset Layout.
- Tests: 7 viewport-maths tests (rays, triangles, boxes, handle picking, drags, rotation sign,
  projection round trip), 2 math tests; the editor smoke test now clicks and drags in the real
  viewport (posted mouse messages to its own child window): pick, drill down, empty click, a 3 m
  X drag, a 40° yaw drag, undo, layout save/restore/reset (37 checks). 96/96 Debug, 90/90 Release
  without GPU.

**Not yet:** snapping, local-space gizmos, scale gizmo, multi-select, box select, an outline
instead of a tint; panels moved to another dock or floated are not remembered.

### 2026-11-15 — Milestone 3, part 1: NoorEditor exists
**Added**
- ADR-025: the engine owns the document (project, registry, scene, undo history, prefabs); the
  editor reads JSON snapshots and changes things only with commands. Editor API **v1**:
  `noor_doc_*` (open/create project, assets, scenes, open/new/save scene, scene snapshot,
  `execute` with `set_transform`/`rename`/`set_component`/`remove`/`reparent`/`add_asset`/
  `begin`/`end`, undo, redo, revision) and `noor_viewport_set_document` / `noor_viewport_frame`.
- Viewports render the document's scene (prefabs expanded, models cached by path) from immutable
  snapshots published by the document; orbit camera (right-drag orbit, middle-drag pan, wheel
  zoom, framing). Native child window: middle button, wheel, capture loss; keys are forwarded to
  the host window so editor shortcuts work while the viewport has focus.
- `editor/NoorEditor`: project hub (recent projects, new, open), docked Viewport / Scene /
  Inspector / Content Browser / Console, menus, status bar, Ctrl+S/Z/Y, F, Delete, unsaved-changes
  prompts, window size remembered. Inspector edits are one undo step each; rotation shown as Euler
  degrees (yaw about +Y); comma decimals accepted.
- Tests: `noor_editor_api_tests` (document API through the DLL, no GPU, runs in CI) and
  `editor.smoke` (builds the editor and runs `--smoke`: 27 checks incl. inspector edits,
  undo/redo, save file contents, delete/undo, floating the viewport, zero validation/engine
  errors). 85/85 locally. CI now builds NoorEditor.

**Fixed**
- Prefab expansion: a non-string entry in an instance's `removed` list was read through a
  default-constructed `Result<Guid>` (which counts as success) and skipped the nil id. Now ignored.

**Not yet:** gizmos, viewport picking, selection highlight, component editors, play mode, layout
persistence (window size only), several viewports on one document sharing a GPU device.

## Milestone 2 complete

### 2026-11-15 — Milestone 2 complete: prefabs, scene rendering, review
**Added**
- ADR-024 (prefabs). `Guid::derive` (RFC 9562 v8, SHA-256) for stable expanded ids.
- `scene::Prefab` (`.noorprefab`, format 1), `expand_prefabs` (overrides: name/layer/tags/
  transform parts/components; `removed` subtrees; nested prefabs and variants; cycle and depth
  detection; unused overrides kept and reported), `make_prefab` (instance keeps the old root's id),
  `make_instance`, `PrefabLibrary` (loads prefabs through the registry).
- Registry: `.noorprefab` files are assets (importer `prefab`, GUID = the prefab's own id).
- `noor_cli make-prefab`, `add-prefab`; `validate` checks the expanded scene.
- `noor_diag --project <dir> --scene <file>`: renders a scene with prefabs expanded; hot-reloads
  when the scene, a prefab or a model changes.
- `docs/milestones/M2_REVIEW.md`: gate review (complete, 5 documented exceptions).
- Tests: 10 prefab tests (incl. exact world-matrix equivalence after making a prefab), CLI workflow
  makes and places prefabs. 80/80 (Debug), 75/75 non-GPU (Release).

**Measured:** street demo, 12 instances of 4 BadBuilding prefabs → 412 entities, 334 draws,
56,306 triangles; Debug + validation CPU 1.7 ms, GPU 0.02 ms.

### 2026-11-15 — Log file follows live
- `FileLogSink` flushes routine lines at least once per second (warnings and errors still at once).
  Found during Tom's hot-reload demo: the log only showed the reloads after the window closed.
- Hot reload demonstrated on Tom's machine: five BadBuilding buildings (panel block, kamienica,
  courtyard block with gate passage, long PRL slab, house) swapped live into a running noor_diag.

### 2026-11-15 — Milestone 2, second increment: undo/redo, format migrations, hot reload
**Added**
- `scene::History` + commands (`set_transform`, `rename`, `set_component`, `add_entity`,
  `remove_entity` with subtree, `reparent` with cycle check): transactions, labels, save tracking
  (`modified()` is false again after undoing back to the saved state), step limit.
- `asset::Migrations`: versioned document upgrades (N → N+1 steps, newer versions refused).
- **Scene format 2**: entities gain `layer` and `tags` (spec 9.1). Format 1 files upgrade on load;
  saving keeps the original as `.bak`.
- `asset::FileWatcher`: polling watcher that reports a change once the file has settled.
- `noor_diag --glb` hot-reloads the model when the file changes (keeps the old model if the new
  file is broken).
- `noor_cli migrate <project> [--dry-run]`; `validate` notes scenes in an older format.
- Tests: 10 new unit tests incl. 25 random 40-edit sequences that must undo to the byte-identical
  original and redo to the byte-identical result; CLI workflow covers migration. 69/69 pass (Debug),
  64/64 non-GPU (Release). Hot reload verified end to end by overwriting a GLB while noor_diag ran.

**Fixed**
- File watcher counted one extra poll before reporting a settled change (found by its test).

**Known limitations**
- No prefabs yet; editor UI for undo/redo comes with NoorEditor.

### 2026-11-15 — Noor Interchange v0.3
- Seeds are unsigned 64-bit; `license.attribution` for automatic credits; `generator.determinism`
  plus cross-machine determinism rules (Thornwake D5). All optional: v0.2 files stay valid.
- Schema updated; the real BadBuilding manifest in `tests/data` validates against it.

### 2026-11-15 — Milestone 2, first increment: projects, asset registry, GLB import, scenes, CLI
**Added**
- ADR-004 (scene model), ADR-005 (project serialization), ADR-006 (asset registry).
- Dependencies: nlohmann/json 3.12 and cgltf 1.15 (MIT, pinned by hash).
- `foundation`: `fs::read_file` (size-limited), `fs::write_file_atomic` (tmp + replace + `.bak`),
  SHA-256 (FIPS vectors tested), `math::Quat` / `to_mat4` / `compose`.
- `noor::asset`: `Project` (create/open, version check, path-escape protection), deterministic
  JSON helpers, `AssetRegistry` (`.noormeta` sidecars; GUID from the Noor Interchange manifest
  when present; re-attaches metas of files moved outside the engine by content hash; duplicate-id
  and orphan handling; in-engine `move_asset`), `import_gltf` (geometry, base colors, TRS
  hierarchy, `extras.noor`, untrusted-input checks).
- `noor::scene`: entities/components document, byte-stable save/load, unknown components kept,
  `instantiate_model` (markers → `marker`, collision → `collider`, never `mesh`), validation.
- `noor_cli`: `new`, `scan`, `info`, `add-model`, `validate` (headless; runs in CI).
- `noor_diag --glb <file>`: renders an imported model with markers.
- Test data: one real BadBuilding block (`tests/data/badbuilding`, 557 KB GLB + manifest + recipe).
- Tests: 22 new unit tests (incl. the M2 rename/move gate), `cli.project_workflow`,
  `render.glb_building`. 59/59 pass (Debug), 54/54 non-GPU (Release).

**Known limitations**
- Base colors only (no textures); LOD1+ files not loaded; `move_asset` doesn't move Noor
  Interchange side files; no undo/redo, prefabs, hot reload or migrations yet.

### 2026-11-15 — Milestone 1 complete: lifecycle test, live redraw, GPU labels
**Added**
- `Window::minimize/restore/set_size/display_index/move_to_display`, `platform::display_count()`
  (each call waits for the OS with `SDL_SyncWindow`).
- `platform::set_live_redraw_callback`: SDL event watch that redraws during the Windows modal
  resize loop (fixes the Milestone 0 "stretched frame while dragging" limitation).
- `noor_diag --lifecycle-test` and CTest `lifecycle.window`: minimize, restore, two resizes and,
  with 2+ displays, a move to another monitor and back; 11 checks. Passes on the dev machine (2 displays).
- `VK_EXT_debug_utils` whenever available: object names (swapchain, depth, meshes, pipeline,
  command buffers, query pool, capture buffer) and command labels (frame, main pass, capture).
  `MeshData::debug_name`.
- `Renderer::swapchain_size()`.
- `docs/milestones/M1_REVIEW.md`: gate review with evidence.

**Fixed**
- The renderer no longer acquires/presents while the window is minimized (it presented one
  extra frame; found by the lifecycle test).

**Known limitations** (accepted Milestone 1 exceptions)
- Device loss is reported, not recovered.
- High-DPI above 100% untested.

36/36 tests pass (Ninja Debug).

### 2026-11-15 — Noor Interchange v0.2
Feedback from the first real exporter (BadBuilding 6d59174):
- Collision: `extras.noor.collision` is authoritative and never rendered; suffix `-colonly` /
  `-convcolonly` replaces `-col` (Godot draws `-col` meshes).
- LODs: one file per level (`<name>.lod<n>.glb`, schema role `lod` + `lod` number); wrapper
  nodes still allowed.
- Markers: new `passage`; `entrance` (place to get in) vs `door` (openable object) clarified;
  more `entrance` kinds; `door.opens`.
- Ids may be UUID v5 from a natural key; tool-specific manifest blocks; `not_run` validation allowed.
- Schema re-validated: examples pass, v0.1 manifests still accepted, LOD files without a level rejected.

### 2026-11-15 — Milestone 1, second increment: input, fly camera, GPU timing, stress test
**Added**
- `platform/input.h`: keyboard (physical scancodes) and mouse state with pressed/released edges,
  mouse deltas, wheel; released on focus loss. `platform::input()`, `set_relative_mouse()`.
- `render/fly_camera.h`: free-fly camera (yaw/pitch, flattened forward, clamped pitch,
  speed/boost); 7 unit tests.
- GPU timestamps per frame (`FrameStats::gpu_ms`) and CPU record time (`cpu_record_ms`).
- `ErrorCode::DeviceLost` with a plain-language message for lost devices (all Vulkan calls via
  `vk_failure`). Detection and reporting only; recovery not implemented.
- `noor_diag`: ground plane, fly camera controls, `--cubes N` grid, timings in the title and an
  exit summary. Verify mode unchanged (fixed camera, hero cube only).
- Test `stress.cubes_2500`: 2,502 draws/frame for 120 frames with validation. 35/35 tests pass
  (Ninja Debug build).

**Measured** (RTX 3080, 1280×720, 2,502 draws): Release CPU record 0.38 ms without validation,
9.0 ms with validation; GPU 0.06–0.09 ms.

**Known limitations**
- One draw call per object; no instancing yet.
- Minimize/restore and monitor changes still untested; device loss is reported, not recovered.

### 2026-11-15 — Continuous integration
**Added**
- `.github/workflows/ci.yml`: Windows, Debug + RelWithDebInfo builds, all tests except `gpu`-labelled
  ones (runners have no GPU), Avalonia spike build. Vulkan SDK from LunarG's installer, cached.
  Actions pinned to commit SHAs.
- `windows-ninja` CMake preset: CI finds the newest MSVC with `vswhere` and builds with Ninja, so
  it doesn't break when runner images change Visual Studio versions (the first run failed exactly
  that way: `windows-latest` has no VS 2022). Verified locally too: 27/27 tests with the Ninja preset.
- CI badge in the README.

**Known limitations**
- GPU tests run only on developer machines. A software Vulkan driver (e.g. lavapipe) could bring
  the render pixel test to CI later.

### 2026-11-15 — Noor Interchange v0.1 (draft)
**Added**
- `docs/interchange/NOOR_INTERCHANGE.md`: shared export rules for all Noor tools (NoorMD benches,
  NoorShip, NoorLevel, NoorArchitect, ...): GLB + `<name>.noor.json` manifest + recipe; markers,
  parts, LODs and collision in `extras.noor`; ADR-022 space conventions.
- `docs/interchange/noor.asset.schema.json` (JSON Schema 2020-12) and two example manifests.
  Examples validate; wrong units, Z-up, unknown kinds, escaping paths and missing ids are rejected.
- ADR-023 (Proposed).

**Known limitations**
- Draft: no tool exports it yet and NoorEngine has no importer yet (Milestone 2).

### 2026-11-15 — Milestone 1, first increment: meshes, camera, depth, GPU pixel tests
**Added**
- `noor/foundation/math.h`: Vec3/Vec4/Mat4, rotation, look_at, reverse-Z infinite perspective;
  7 unit tests locking in ADR-022 conventions (right-handed, +Y up, column-major, reverse-Z).
- ADR-022 (math and coordinate conventions).
- Build-time HLSL → SPIR-V with DXC (`cmake/NoorShaders.cmake`), embedded into the binary.
- VMA 3.5 for GPU memory.
- Renderer: dynamic rendering, D32 reverse-Z depth buffer, swapchain image views, mesh pipeline
  (lit vertex colors), `create_mesh`/`destroy_mesh` (staged upload, index validation),
  `FrameDesc` with camera and draw list, per-frame stats, frame capture (`request_capture` /
  `take_capture`). Renderer split into `vulkan_renderer.cpp`, `vulkan_resources.cpp`,
  `vulkan_capture.cpp` with a shared internal header.
- `noor_diag`: lit spinning cube, `--verify-render` (pixel probes) and `--screenshot` (BMP).
- Test `render.cube_pixels`: checks background, front face and top face colors in a real frame.
  Verified that it fails when triangle winding is deliberately broken. 27 tests pass.

**Known limitations**
- Mesh upload blocks on the GPU; mesh destruction waits for device idle.
- Single hard-coded shader and light; editor viewport still clears only.
- Not done yet for Milestone 1: input services, GPU timing markers, device-loss handling, stress tests.

## Milestone 0

### 2026-11-15 — Avalonia editor spike (ADR-003 accepted)
**Added**
- `platform::PresentationTarget`: interface the renderer presents to. `Window` (SDL) and the new
  `NativeChildWindow` (Win32 child window inside a foreign parent, e.g. the editor UI) implement it.
  The renderer no longer depends on SDL windows.
- `noor_editor_api.dll` (v0, spike-grade C ABI): log forwarding, viewports (native child window +
  dedicated render thread, live stats). Destroy pumps UI messages while joining the render thread.
- `spikes/avalonia_viewport`: Avalonia 12.1.4 + Dock 12.1.0.13 editor shell with a docked Vulkan
  viewport, live Inspector/Console/status bar, labeled sample panels, and `--smoke` automated checks.
- Devlog #003 with real screenshots.

**Measured** (Debug, validation on, RTX 3080, 60 Hz, 100% scale): 60 FPS; 236 ms viewport → first
frame; 1.6 s process start → first frame; resize reaches swapchain; float into own window re-renders
after 363 ms; 0 validation errors; 0.74% machine CPU; 247 MB working set. Details in ADR-003.

**Fixed during development**
- Spike screenshots first used a screen BitBlt, which captured another application overlapping the
  window. Deleted immediately; capture now uses `PrintWindow`, which only renders our own window.

**Tested by hand** (Tom): hover/click input reaches the viewport (hue follows the mouse, click
brightens); drag-and-drop docking with the mouse works.

**Known limitations**
- Untested: high-DPI >100%, dock drop indicators over the viewport itself (airspace), live
  drag-resize, keyboard focus, Release numbers.
- One Vulkan instance + device per viewport (floating costs ~360 ms); needs a shared device.
- Floating Dock windows show the default blue accent instead of the theme purple.
- `renderer.h` now includes `vulkan.h` indirectly through `presentation_target.h`, weakening
  ADR-002's "no Vulkan types above render/" rule. To be tidied when the RHI layer arrives.

### 2026-11-15 — Devlog website
**Added**
- `website/`: static site (home with the README "Honest status" table, devlog index, one page
  per `docs/devlog/DEVLOG_NNN_*.md`, ADR index, Atom feed, 404). Generated by a small Node script;
  one dependency, `marked` 18.1.2 (MIT), pinned with a lockfile. Dark-first with a light mode.
  Editor mockup shown only with a CONCEPT ART label.
- New devlog files appear on the site automatically; relative links are rewritten to site pages
  or GitHub. 12 unit tests for the build helpers (`npm test` in `website/`).
- `.github/workflows/pages.yml`: tests, builds and deploys to GitHub Pages on push to `main`
  (site, devlog, ADR index, README or workflow changes) and on manual run; PRs build only.

**Known limitations**
- Not yet deployed or run on GitHub Actions at the time of writing. Workflow YAML was not
  checked with a linter locally.

### 2026-11-15 — Repository foundation
**Added**
- Repository layout, MIT license (ADR-019), CONTRIBUTING, IDEAS parking lot, project CLAUDE.md.
- ADRs: 001 C++23/CMake/MSVC (Accepted), 002 Vulkan 1.3 (Accepted), 003 Avalonia editor GUI
  (Proposed, spike pending), 019 MIT license (Accepted), 021 SDL3 + dependency policy (Accepted).
- CMake build with presets; dependencies pinned by commit hash (SDL 3.4.18, doctest 2.5.3);
  `third_party/manifest.json`.
- `noor::foundation`: structured thread-safe logging (console, file, memory sinks; per-level
  counters), `Result<T>`/`Error` error handling, `NOOR_ASSERT` with pluggable handler, v4 GUIDs.
- `noor::platform`: SDL3 init/shutdown, event pump, high-DPI aware window, Vulkan surface bridge.
- `noor::render`: Vulkan 1.3 instance/device/swapchain, 2 frames in flight, sync2 barriers,
  clear-to-color frame loop, swapchain rebuild on resize/out-of-date, validation message capture.
- `noor_diag` diagnostics tool: reports GPU, clears to Noor purple (sRGB-correct), FPS in title.
- Tests: 18 unit tests (log, error, assert, GUID) and a GPU smoke test (180 frames, validation on,
  fails on any validation error). Debug and RelWithDebInfo both pass on the dev machine.

**Fixed during development**
- Vulkan loader errors from third-party implicit layers (ReShade on the dev machine) were counted
  as validation errors. Now reported as `[loader]` warnings; real validation errors still fail.
- Startup no longer builds the swapchain twice (resize event with unchanged size is ignored).

**Known limitations**
- Window resize and maximize tested by hand by Tom (1280×720 → 222×161 → 1920×1017; swapchain
  rebuilt correctly, 1,311 frames, zero validation errors). Minimize/restore and monitor changes
  remain untested. No automated resize test yet.
- No live redraw while dragging a window edge: Win32's modal resize loop pauses our frame loop,
  so the swapchain rebuilds only when the drag ends and Windows stretches the last frame meanwhile.
  Fix before the editor (render from an SDL event watch during resize).
- No negative test yet proving a deliberate validation error fails the smoke test.
- Windows only. No CI yet (GitHub Actions runners have no GPU; unit tests can run there).
