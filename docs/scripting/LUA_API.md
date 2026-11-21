# NoorEngine Lua API (Luau)

Status: **Milestone 4, parts 2–7**. The API grows each part;
everything here is implemented and tested (`tests/unit/test_lua_scripts.cpp`).

NoorEngine's Lua is **Luau** (ADR-009): Lua 5.1 syntax plus `continue`, compound assignment
(`+=`), string interpolation, optional type annotations and a native `vector` type. Scripts run
in a sandbox: no file, OS or process access; built-in libraries and globals are read-only.

## Attaching a script

In the Inspector: **Add component ▸ Lua script…** creates `Scripts/<Name>.luau` from a template
and attaches it. Under the hood the entity gets:

```json
"script": { "file": "Scripts/Spinner.luau", "params": { "speed": 90 } }
```

Edit `params` in the Inspector (**+ Param**); the script reads them as `self.params.speed`.

## Shared code: `require`

Put functions several scripts use in their own file that returns a table, and `require` it:

```lua
-- Scripts/util.luau
local util = {}
function util.clamp(x, a, b) return math.max(a, math.min(b, x)) end
return util

-- any script
local util = require("util")            -- Scripts/util.luau (or require("Scripts/util.luau"), or a path in the project)
```

A file is loaded once and every script gets the same table (so it can hold shared state too). Paths stay inside the
project; a file that requires itself through others is an error that says so (gameplay foundations, part 1).

## A script

```lua
local Spinner = {}

function Spinner:on_start()
    noor.log("spinning", self.entity:name())
end

function Spinner:on_update(dt)
    self.entity:rotate(vector.create(0, 1, 0), self.params.speed * dt)
end

return Spinner
```

The file returns a table of hooks. Each entity gets its own instance (`self`), so fields you
store on `self` belong to that entity. Engine-set fields: `self.entity`, `self.params`.

| Hook | When |
|---|---|
| `on_start()` | Once, when play starts (or the entity appears) |
| `on_update(dt)` | Every frame; `dt` in seconds |
| `on_fixed_update(dt)` | At a fixed 60 Hz, after `on_update` (physics will run here) |
| `on_late_update(dt)` | Every frame, after the fixed steps |
| `on_destroy()` | When the entity is destroyed, or play stops |
| `on_collision_enter(other)`, `on_collision_exit(other)` | Physics bodies start / stop touching (before `on_late_update`) |
| `on_trigger_enter(other)`, `on_trigger_exit(other)` | Something enters / leaves a trigger collider |
| `on_animation_end(name)` | A non-looping animation has finished: a sprite's or a model clip this entity's script played, or a controller state (any layer) (before `on_late_update`) |
| `on_water(state)` | A character's state in the water changed: "dry", "wading", "swimming", "under" (8D) |
| `on_animation_event(name)` | A named moment of the playing animation was passed (footsteps...: the animator's `"events"`, or a controller state's) |

Errors stop that script for that entity (the Console shows file, line and stack); the game keeps
running. A hook that runs longer than 0.25 s is stopped (endless loops).

## `noor`

| Function | Returns |
|---|---|
| `noor.log(...)`, `noor.warn(...)` | Writes to the Console with the script's file and line |
| `noor.time()` | Seconds since play started |
| `noor.find(name)` | The first entity with that name, or `nil` |
| `noor.find_all(tag)` | Every entity with that tag, in scene order (a **group**: `"enemy"`, `"pickup"`...). Tags are set in the Inspector or by `e:add_tag` |
| `noor.spawn(prefab, position [, euler [, parent]])` | A new copy of a prefab (its asset id, or its path like `"Assets/Bullet.noorprefab"`) at `position`, turned by `euler` (degrees), under `parent` if given (then position and turn are local to it). Returns the prefab's root entity (destroying it takes the rest with it), or `nil` if there's no such prefab. It starts next frame: its scripts' `on_start`, bodies, sprites and sounds |
| `noor.input.down(action)` | Action held this frame |
| `noor.input.pressed(action)` | Action went down this frame |
| `noor.input.released(action)` | Action went up this frame |
| `noor.input.axis(name)` | `-1..1` for key axes; mouse movement in pixels for `look_x` / `look_y` |
| `noor.raycast(origin, direction, max_distance [, ignore])` | The first physics body hit: `{entity, point, normal, distance}`, or `nil` |
| `noor.shape_cast(shape, from, to [, ignore])` | A shape swept from `from` to `to`: the first hit, as raycast (`distance` along the way). `shape` is a table like a collider: `{shape = "circle", radius = 0.3}`, box, sphere, capsule, rectangle |
| `noor.overlap(shape, at [, ignore])` | A list of the entities whose bodies overlap `shape` placed at `at` |
| `noor.water_height(x, z)` | The water surface's height (world y) at world (x, z) now, waves included; `nil` where there's no water (8D) |
| `noor.water_flow(x, z)` | The water's own velocity there (m/s: a river's current; still water: zero), `nil` where there's no water (8D) |
| `noor.water_normal(x, z)` | The water surface's normal there (a vector), or `nil` |
| `noor.play_sound(clip [, position [, volume [, bus]]])` | Plays a sound once: `clip` is an asset id or a path like `"Assets/Audio/step.wav"`; with a `position` (vector) it's 3D, without it plays everywhere. Default bus `sfx`. Returns `true` if it started |
| `noor.set_volume(bus, volume)` | Bus volume (`master`, `music`, `sfx`, `ui`, `voice`), `0` is silent, `1` normal |
| `noor.ui.text(...)`, `noor.ui.rect(...)` | The HUD: see [The HUD](#the-hud) |
| `noor.ui.open(path)`, `noor.ui.find(path)` | UI documents (menus): see [UI documents](#ui-documents-menus) |
| `noor.input.capture_mouse(on)` | Ask to capture the mouse for looking (`true`) or release it. Works once the player has clicked into the game view; **Esc** always releases it |
| `noor.input.mouse_captured()` | Whether the mouse is captured now (then `look_x` / `look_y` are raw mouse movement) |

Default actions: `jump` (Space), `sprint` (Shift), `crouch` (Ctrl, C), `fire` (left mouse),
`aim` (right mouse), `interact` (E), `pause` (Escape). Axes: `move_x` (A/D, ←/→), `move_y`
(S/W, ↓/↑). Change or add them in the project's `Settings/input.json`:

```json
{ "actions": { "jump": ["Space", "J"] },
  "axes": { "turn": { "negative": ["Q"], "positive": ["E"] } } }
```

## Entities

All transforms are **local** (relative to the parent) unless the name says *world*. Angles are
degrees; Euler angles are the Inspector's (pitch X, yaw Y, roll Z). Vectors are Luau vectors:
`vector.create(x, y, z)`, with `+ - *`, `vector.magnitude`, `vector.normalize`, `vector.dot`,
`vector.cross`.

| Method | |
|---|---|
| `e:name()`, `e:id()`, `e:exists()`, `e:parent()` | Identity |
| `e:child(name)` | The first direct child with that name, or `nil` |
| `e:bone(name)` | The first entity of that name anywhere below (a model's bone: read where a hand is, or parent a prop to it), or `nil` |
| `e:position()`, `e:set_position(v)` | Local position |
| `e:translate(v)` | Move in the parent's axes |
| `e:translate_local(v)` | Move along the entity's own axes |
| `e:euler()`, `e:set_euler(v)` | Rotation as Euler degrees |
| `e:rotate(axis, degrees)` | Turn about an axis (parent space) |
| `e:scale()`, `e:set_scale(v)` | Scale |
| `e:world_position()` | Position in the world |
| `e:forward()`, `e:right()`, `e:up()` | World directions (forward is the entity's −Z) |
| `e:get(component)` | The component's data as a table (a copy), or `nil` |
| `e:set(component, table)` | Replaces the component's data (`nil` removes it) |
| `e:destroy()` | Removes the entity and its children at the end of the frame |
| `e:teleport(position [, euler])` | Puts it there **at once** (world position): a character or physics body through physics, with its velocity, jump and walking speed reset (a respawn, a portal); anything else just moves. `euler` turns it too. `true` if it exists. (`set_position` on a character or body is undone by physics: use this) |
| `e:has_tag(tag)`, `e:add_tag(tag)`, `e:remove_tag(tag)` | Groups (Godot's `is_in_group`, `add_to_group`) |
| `e:velocity()`, `e:set_velocity(v)` | Linear velocity of its physics body (m/s). On a character: with `"motion": "script"` it's the character's own velocity (see below); otherwise `set_velocity` replaces its velocity for the next step (a double jump, a dash, a knockback) |
| `e:is_on_floor()`, `e:is_on_wall()`, `e:is_on_ceiling()`, `e:floor_normal()` | Character: what it touched in its last step (Godot's names) |
| `e:tile(x, y)`, `e:set_tile(x, y, tile)` | A `tilemap`'s cell ((0, 0) its bottom-left): the tile there (-1 empty; nil without a tilemap); set one (-1 or nil empties it; true if inside the map). Drawing and collision follow ([Tilemaps](../fx/TILEMAPS.md)) |
| `e:add_impulse(v)` | Instant push (kg·m/s), e.g. a jump or a hit |
| `e:add_force(v)` | Push during the next physics step (newtons) |
| `e:character_move(v)` | Character controller: walk with this horizontal velocity (m/s, world) this frame. Call it every frame you want to move |
| `e:character_jump(speed)` | Character controller: jump (upward m/s, default 5) if standing on ground (or within its `coyote_time` of leaving it; with a `jump_buffer`, a jump asked just before landing happens on landing) |
| `e:character_jump_release()` | The jump button was let go: a jump still rising keeps `jump_cut` (0.5) of its upward speed, so a tap jumps lower than a hold |
| `e:character_drop()` | Falls through the one-way platform it stands on |
| `e:is_grounded()` | Character controller: standing on walkable ground |
| `e:character_water()` | Character controller in water (8D): its state ("dry", "wading", "swimming", "under"), the water's depth at its feet and at its head (positive: under); `nil` if it isn't a character |
| `e:play_sound()`, `e:stop_sound()`, `e:is_playing()` | The entity's `audio_source`: (re)start, stop, still playing? |
| `e:play_animation(name [, speed])` | The entity's `animator` (a model's clip, or with a controller a state of its base layer) or else its `sprite`: plays it from the start (speed 1 by default); a non-looping one calls `on_animation_end(name)` when done. `false` with neither ([animation](../animation/ANIMATION.md), [sprites](../fx/SPRITES.md)) |
| `e:stop_animation()`, `e:set_animation_speed(s)`, `e:animation()` | Freeze it where it is; play on faster or slower from here (0 freezes); what it's playing (a controller: its base layer's state) |
| `e:cross_fade(name, seconds [, speed])` | An `animator`: blends from what's playing to `name` over `seconds` (a controller: to that state) |
| `e:set_animation_parameter(name, value)`, `e:set_animation_trigger(name)` | An `animator`'s controller parameter (a number or a boolean); a trigger is a boolean the transition that uses it resets |
| `e:burst(count)` | The entity's `emitter`: `count` particles at the next fixed step. Change its settings with `e:set("emitter", {...})`: its particles carry on |

## Physics components

Add them in the Inspector (**Add component ▸ Physics**) or use **Create ▸ Physics crate / Physics ball / Floor**.

```json
"collider":  { "shape": "box", "size": [1, 1, 1] }          // or sphere (radius), capsule (radius, height),
                                                            // mesh (static: the model's own triangles)
             // optional: "offset": [x, y, z], "trigger": true
"rigidbody": { "type": "dynamic", "mass": 1, "friction": 0.5, "restitution": 0,
               "linear_damping": 0.05, "gravity_scale": 1 }  // type: dynamic | kinematic | static
```

```json
"character": { "radius": 0.3, "height": 1.8, "max_slope": 50, "step_height": 0.4, "mass": 70 }
"camera":    { "fov": 70, "near": 0.05 }
```

```json
"light":     { "type": "directional", "color": "#FFF5E6", "illuminance": 100000 }  // the sun (lux)
"camera":    { "ev100": 14, "tonemapper": "neutral" }                             // exposure
```
The sun shines along its entity's forward; see [docs/rendering/PBR.md](../rendering/PBR.md).

**2D physics** ([ADR-040](../decisions/ADR-040-2d-and-isometric.md)): the 2D world is the XY plane (+Y up). 2D shapes
are slabs (`depth` thick, 1 m by default) so 2D bodies always meet; a dynamic body with `"plane": "xy"` moves only in X
and Y and turns only about Z, exactly (it never drifts out of its plane):

```json
"collider":  { "shape": "rectangle", "size": [2, 1] }        // or circle (radius), polygon (convex "points": [[x, y], ...])
"rigidbody": { "type": "dynamic", "plane": "xy" }
"rigidbody": { "type": "dynamic", "lock_rotation": ["x", "z"] }   // any axes, in 3D too ("lock_position" likewise)
```

A `circle` rolls like a disc (it's given a disc's inertia). `angular_damping` (0.05 by default) slows turning, as
`linear_damping` slows moving.

**2D characters and platformers** (2D foundations, part 2): a `character` with `"plane": "xy"` moves along X only and
never leaves its plane. Its feel is set on the component (all off by default, so 3D characters move as before):

```json
"character":  { "radius": 0.3, "height": 1.1, "plane": "xy", "gravity_scale": 3,
                "acceleration": 40, "deceleration": 50, "air_acceleration": 25,   // m/s2; 0: at once
                "coyote_time": 0.1, "jump_buffer": 0.1, "jump_cut": 0.5 }
"platformer": { "speed": 6.5, "jump_height": 2.2,
                "animations": { "idle": "idle", "run": "run", "jump": "jump", "fall": "fall" } }
"collider":   { "shape": "rectangle", "size": [3, 0.25], "one_way": true }      // jump up through it, stand on it
```

- **`coyote_time`**: a jump still works this many seconds after running off a ledge. **`jump_buffer`**: a jump pressed
  this long before landing happens on landing. Both forgive the player's timing, the way good platformers do.
- **`platformer`** is a built-in player: `move_x` walks, `jump` jumps to exactly `jump_height` metres (held; a tap
  jumps lower), down + jump drops through a one-way platform. It faces the sprite the way the character moves and
  plays the named sprite animations. Without keys pressed, a script can steer the same character with
  `character_move` / `character_jump`, and the facing and animations still follow.
- **One-way platforms** are level: they're stood on from above only.

**Your own controller, Godot-style** (2D part 2b): give the character `"motion": "script"` and your script owns its
velocity, like a CharacterBody in Godot: NoorEngine adds no gravity and no jumps. Set the velocity every fixed step; the
character moves, slides along whatever it touches, and `e:velocity()` next step is what's left after sliding (landing
stops a fall, a wall stops a run). [Coming from Godot](COMING_FROM_GODOT.md) maps the rest.

```lua
function Player:on_fixed_update(dt)
    local e = self.entity
    local v = e:velocity()                          -- velocity after last step's slide
    v = vector.create(noor.input.axis("move_x") * 6, v.y - 29.4 * dt, 0)
    if e:is_on_floor() and noor.input.pressed("jump") then v = vector.create(v.x, 12, 0) end
    e:set_velocity(v)                               -- like move_and_slide: it moves at the next physics step
end
```

A `character` is a capsule controller with its feet at the entity's origin: it slides along
walls, walks up steps and slopes, stays on the ground going down, and jumps. While playing, the
viewport looks through the first entity with a `camera`. **Create ▸ Player (first person)** sets
up both, with `Scripts/PlayerController.luau` as a starting point.

A collider without a rigidbody is static. Kinematic bodies follow their entity (move them from a
script). Physics runs at 60 Hz, after `on_fixed_update`. BadBuilding models exported with
collision meshes (Noor Interchange `-colonly`) get static colliders automatically.

Entities compare with `==`; a destroyed entity's methods return `nil` and do nothing.

## Sound components

Drag a `.wav`, `.flac` or `.mp3` into `Assets/`, then **Add to scene** in the Content Browser, or
**Add component ▸ Sound**.

```json
"audio_source":   { "clip": "<asset id or Assets/... path>", "volume": 1, "pitch": 1, "loop": false,
                    "spatial": true, "min_distance": 1, "max_distance": 60, "bus": "sfx",
                    "play_on_start": true, "stream": false }   // stream: for long music files
"audio_listener": {}                                         // the ears; normally on the camera
```

Spatial sounds get quieter with distance (full volume inside `min_distance`, no further fading
past `max_distance`) and pan with the listener's orientation. Without an `audio_listener` the
first `camera` hears.

## Messages between scripts

Gameplay foundations, part 3: Godot's signals. An entity **says** something; any script can **listen**:

```lua
-- Lever.luau: say it
self.entity:emit("pulled", { strength = 3 })

-- Door.luau: listen to the lever
function Door:on_start()
    noor.find("Lever"):on("pulled", function(data, sender)
        if data.strength >= 3 then self:open() end
    end)
end

-- game-wide, from anyone to anyone
noor.emit("level_complete", { time = 92.5 })
noor.on("level_complete", function(data) noor.log("done in " .. data.time) end)

-- or ask another script directly (Godot's door.open())
local open = noor.find("Door"):call("is_open")
```

| | |
|---|---|
| `e:emit(name [, data])` | `e` says `name`; everyone listening to `e` hears it **now**, in the order they started listening. Returns how many heard it |
| `e:on(name, fn(data, sender))` | The running script listens to `e`. Returns a listener (`l:cancel()`) |
| `noor.emit(name [, data])`, `noor.on(name, fn(data, sender))` | Game-wide messages (`sender` is `nil`) |
| `e:call(function, ...)` | Calls a function of `e`'s script, whether it's Lua or C#, and returns its result. An error if there's no such function |

- **Lua and C# hear each other**: messages and calls cross languages. Data travels as JSON: numbers, strings, booleans,
  tables; an entity in the data arrives as its id (`sender` is a real entity).
- **Engine events are messages too**, from the entity they happen to: `collision_enter`, `collision_exit`,
  `trigger_enter`, `trigger_exit` (data: `{other = id}`), `animation_end`, `animation_event`, `water` (`{name = ...}`).
  A door can listen to a pressure plate's `trigger_enter` without a script on the plate.
- Listeners go when the listening entity or the one it listens to is destroyed. A listener that errors is reported and
  stopped; the others still hear. A message sent from its own listeners 32 deep is stopped with an error (a loop).

## Tweens: smooth changes in one line

Gameplay foundations, part 4 (Godot's Tween):

```lua
door:tween("position", vector.create(0, 3, 0), 0.5, "ease_out")                  -- slide up in half a second
lamp:tween("light.intensity", 0, 2.0, "sine_in_out")                             -- fade a light out
sign:tween("sprite.color", "#FF4040", 0.3)                                       -- blend a colour
platform:tween("position", vector.create(5, 0, 0), 2, "ease_in_out", { loops = -1, yoyo = true })  -- back and forth forever
door:tween("position", up, 0.5, "ease_out", { on_done = function() noor.play_sound("Assets/Audio/clunk.wav") end })

noor.start(function()                                                             -- one after another
    door:tween("position", up, 0.5, "back_out"):wait()
    door:tween("scale", vector.create(1.2, 1.2, 1.2), 0.2):wait()
end)
```

`e:tween(property, to, seconds [, ease [, options]])` returns a tween: `t:cancel()`, `t:active()`, and `t:wait()` inside
a `noor.start` sequence.

- **Properties:** `position`, `euler` (degrees, each angle on its own: 0 to 360 is a whole turn), `rotation` (the
  shortest turn to those Euler angles), `scale`, `world_position`, and `"component.field"` holding a number, numbers or a
  `"#RRGGBB"` colour (`"light.intensity"`, `"camera.height"`, `"sprite.color"`).
- **Easings:** `linear`, `ease_in` / `ease_out` / `ease_in_out`, and `quad_`, `cubic_`, `sine_`, `back_` (overshoots),
  `elastic_`, `bounce_` with `in`, `out` or `in_out`.
- **Options:** `delay` (seconds; it starts from wherever the value is then), `loops` (`-1` forever), `yoyo` (every other
  play goes back), `real` (the real clock: runs while paused), `on_done` (called the frame after it ends).
- A new tween of the same entity and property replaces the running one; a tween ends exactly on its target; one whose
  entity goes stops quietly. On the game's clock, tweens pause with the game and follow slow motion.
- Tween a **kinematic** body to move it (a moving platform); a dynamic body's position belongs to physics.

## Timelines

Gameplay foundations, part 5: keyframed animation of anything, made in the editor's **Timeline** tab
([Timelines](../animation/TIMELINES.md)).

| | |
|---|---|
| `e:play_timeline(name [, speed])` | Plays its animation player's timeline from the start; `false` if it has none by that name |
| `e:stop_timeline()`, `e:timeline()` | Stops it where it is; what it's playing (or `nil`) |

When one that doesn't loop ends, the entity sends the message `timeline_end` (`{name = ...}`): listen with
`e:on("timeline_end", fn)`.

## Time: timers, waiting, pause

Gameplay foundations, part 2. All of it is on the **game's clock** (`noor.time()`), which stops while paused and runs
slower in slow motion, unless you ask for the real one.

```lua
noor.after(2, function() door:destroy() end)              -- once, two seconds from now
local blink = noor.every(0.5, function() lamp_on = not lamp_on end)
blink:cancel()                                            -- stop it
noor.every(1, function() hp += 1; return hp < 100 end)    -- returning false stops it too

noor.start(function()                                     -- a little sequence that waits
    noor.wait(1)
    self.entity:play_animation("open")
    noor.wait(0.5)
    noor.play_sound("Assets/Audio/creak.wav")
end)
```

| Function | |
|---|---|
| `noor.after(seconds, fn [, {real = true}])` | Calls `fn` once, that much later. Returns a timer (`t:cancel()`) |
| `noor.every(seconds, fn [, {real = true}])` | Calls `fn` every `seconds` (at most once a frame) until it returns `false` or is cancelled |
| `noor.start(fn)` | Runs `fn` now as a **sequence**: inside it, `noor.wait` pauses it and carries on later |
| `noor.wait(seconds [, {real = true}])` | Inside a sequence only (anywhere else it's an error that says so) |
| `noor.set_paused(on)`, `noor.paused()` | Pause the game: its clock stops, so physics, animation, particles and game-time timers stop. Only scripts with **Runs while paused** (`"pause": "always"` on the script component) keep running: a pause menu. The paused scripts' HUD stays on screen under it |
| `noor.set_time_scale(s)`, `noor.time_scale()` | How fast game time runs: `0.25` slow motion, `1` normal (0..10). Physics keeps its fixed step size and takes fewer of them, so it stays deterministic |
| `noor.real_time()` | Seconds really passed since play started, paused or not |

- Timers and sequences belong to the script that made them: when its entity is destroyed, they go too.
- `{real = true}` puts them on the real clock (they run while paused, on scripts that run while paused).
- They're checked once a frame, before any `on_update`, in the order they were made: a timer for 0.5 s fires on the first
  frame at or after 0.5 s. Built games replay them exactly.
- A timer or sequence that errors is reported and stopped; the script carries on.
- A pause or time-scale change made in a script applies to the rest of that frame at once (the world doesn't take one
  more physics step).

## The HUD

Text and rectangles drawn over the game. Draw them **every frame** you want them shown (in
`on_update`); what isn't drawn this frame is gone.

```lua
function Hud:on_update(dt)
    noor.ui.text("Score: " .. self.score, 20, 20, { size = 32 })
    noor.ui.rect(0, 0, 4, 4, { color = "#FFFFFFC0", anchor = "center" })        -- a crosshair
    noor.ui.rect(20, -20, 200 * self.health, 12, { color = "#E04040", anchor = "bottom_left" })
end
```

| | |
|---|---|
| `noor.ui.text(text, x, y [, {size, color, anchor}])` | Text; `size` is the line height (default 24); `\n` starts a new line |
| `noor.ui.rect(x, y, w, h [, {color, anchor}])` | A filled rectangle |

**UI units:** the screen is always **720 units tall** (as wide as its shape gives: 1280 on a 16:9
screen), so a HUD looks the same at any window size. +x is right, +y is down.
**Anchor** (`top_left` default, `top`, `top_right`, `left`, `center`, `right`, `bottom_left`,
`bottom`, `bottom_right`) is both the point of the screen `x, y` is measured from and the point of
the element placed there: `anchor = "center"` at `0, 0` centres it; `bottom_right` at `-20, -20`
puts it 20 units in from the corner. Text lines align the same way (left, centred, right).
**Colors:** `"#RRGGBB"` or `"#RRGGBBAA"` (alpha: `80` is half see-through). A wrong color or
anchor is a script error that names the problem.

The built-in font is Atkinson Hyperlegible (SIL OFL 1.1), sharp at any size, with Latin, Latin-1
and Latin Extended-A (Polish, Czech, German, French…) plus € ° – — … ‘ ’ “ ” •. Other characters
show as `?`. Everything is drawn over the scene in the order you draw it (later on top).

## UI documents (menus)

Pages made in `.noorui` files (HTML/CSS-like: [Game UI](../ui/GAME_UI.md)), drawn over the game. A document is a value
you call methods on; elements are named by their `id` (`"resume"` or `"#resume"`). An element that isn't there is a
script error naming it.

```lua
local menu = noor.ui.open("UI/pause.noorui")     -- opened and shown; closes when this script's entity goes
menu:on("click", "resume", function(e) menu:hide(); noor.set_paused(false) end)
menu:on("change", function(e) noor.log(e.element .. " is now " .. tostring(e.value)) end)
menu:set_text("title", "Paused at " .. noor.time())
local hud = noor.ui.find("UI/hud.noorui")          -- one a ui_document component opened
```

| | |
|---|---|
| `noor.ui.open(path)` | Opens and shows a document; it belongs to the running script's entity |
| `noor.ui.find(path)` | A document already open (from a `ui_document` component), or `nil` |
| `d:show()`, `d:hide()`, `d:visible()`, `d:close()` | Shown again (it takes the keyboard), hidden, is it shown, closed for good |
| `d:on(event [, element], fn(e))` | Listen to `click`, `change`, `submit`, `focus`, `blur`, `mouseover`, `mouseout` on one element or any; `e` is `{type, element, value}`; returns a listener (`:cancel()`) |
| `d:set_text(element, text)` | Its content becomes this text, shown as it is (numbers too) |
| `d:set_rml(element, markup)` | Its content becomes this markup |
| `d:set_class(element, name [, on])` | Adds (or with `false` removes) a class |
| `d:set_attribute(element, name, value)` | `"disabled"`, `"src"`...; `true` sets it, `false` or `nil` removes it |
| `d:set_style(element, property, value)` | A style property as RCSS (`"width", "40dp"`); `nil`: back to the style sheet's |
| `d:value(element)` | Text boxes: a string, ranges: a number, checkboxes: `true` / `false`, others: their content |
| `d:set_value(element, value)` | The same kinds |
| `d:focus(element)`, `d:click(element)` | Gives it focus; presses it (its `click` comes next frame) |

Events from the mouse and keyboard arrive in the same frame, those from `set_value` and `click` in the next. A menu's
script usually runs while paused (`"pause": "always"` on its script component).

## Not yet
Saving and loading, levels and scene changes, navigation: the gameplay foundations
([plan](../milestones/M9_GAMEPLAY_PLAN.md)), parts 7 to 9. Binding a page to a script's data, and gamepads, come with
the rest of part 6.
