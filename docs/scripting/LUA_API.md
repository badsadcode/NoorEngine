# NoorEngine Lua API (Luau)

Status: **Milestone 4, parts 2–3**. The API grows each part (physics, audio and more arrive next);
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

Errors stop that script for that entity (the Console shows file, line and stack); the game keeps
running. A hook that runs longer than 0.25 s is stopped (endless loops).

## `noor`

| Function | Returns |
|---|---|
| `noor.log(...)`, `noor.warn(...)` | Writes to the Console with the script's file and line |
| `noor.time()` | Seconds since play started |
| `noor.find(name)` | The first entity with that name, or `nil` |
| `noor.input.down(action)` | Action held this frame |
| `noor.input.pressed(action)` | Action went down this frame |
| `noor.input.released(action)` | Action went up this frame |
| `noor.input.axis(name)` | `-1..1` for key axes; mouse movement in pixels for `look_x` / `look_y` |
| `noor.raycast(origin, direction, max_distance [, ignore])` | The first physics body hit: `{entity, point, normal, distance}`, or `nil` |
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
| `e:velocity()`, `e:set_velocity(v)` | Linear velocity of its physics body (m/s) |
| `e:add_impulse(v)` | Instant push (kg·m/s), e.g. a jump or a hit |
| `e:add_force(v)` | Push during the next physics step (newtons) |
| `e:character_move(v)` | Character controller: walk with this horizontal velocity (m/s, world) this frame. Call it every frame you want to move |
| `e:character_jump(speed)` | Character controller: jump (upward m/s, default 5) if standing on ground |
| `e:is_grounded()` | Character controller: standing on walkable ground |

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

A `character` is a capsule controller with its feet at the entity's origin: it slides along
walls, walks up steps and slopes, stays on the ground going down, and jumps. While playing, the
viewport looks through the first entity with a `camera`. **Create ▸ Player (first person)** sets
up both, with `Scripts/PlayerController.luau` as a starting point.

A collider without a rigidbody is static. Kinematic bodies follow their entity (move them from a
script). Physics runs at 60 Hz, after `on_fixed_update`. BadBuilding models exported with
collision meshes (Noor Interchange `-colonly`) get static colliders automatically.

Entities compare with `==`; a destroyed entity's methods return `nil` and do nothing.

## Not yet
Audio, spawning prefabs, timers and
coroutines-based waits, and require-ing other script files arrive in later parts of Milestone 4.
