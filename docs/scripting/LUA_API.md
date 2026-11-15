# NoorEngine Lua API (Luau)

Status: **Milestone 4, part 2**. The API grows each part (physics, audio and more arrive next);
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

Entities compare with `==`; a destroyed entity's methods return `nil` and do nothing.

## Not yet
Physics (raycasts, forces, the character controller), audio, spawning prefabs, timers and
coroutines-based waits, and require-ing other script files arrive in later parts of Milestone 4.
