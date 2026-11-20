# Coming from Godot

Tom (and many people) learned to make games in Godot. This page maps what you know there to NoorEngine, and says
plainly what NoorEngine doesn't have yet and when it's planned. It's kept up to date as features land.

The list of Godot features below comes from a real game: an old 2D platformer of Tom's, whose scripts lean on Area2D
(252 uses), signals (194), Timer (169), TileMap (103), AnimationPlayer (76), Camera2D (52), CharacterBody2D with
`move_and_slide` and `is_on_floor`, Tween, and instancing scenes. Those are the things a NoorEngine game needs too.

## The big ideas

| Godot | NoorEngine | Notes |
|---|---|---|
| A **node tree**; each node is one thing (Sprite2D, CollisionShape2D...) | **Entities** with **components**; entities can have children | One entity carries several components (a sprite, a collider, a script). Fewer, richer objects than Godot's tree |
| A **scene** (`.tscn`) | A **scene** (`.noorscene`) | Plain JSON you can read and diff |
| Instancing a scene inside another | A **prefab** (`.noorprefab`), with overrides | In the editor today; **from scripts at runtime: not yet** (planned: gameplay foundations) |
| **GDScript** | **Lua** (Luau) or **C#** | Lua is the GDScript-like one: quick, no compiling. `self.params` are your `@export` variables |
| `_ready`, `_process`, `_physics_process` | `on_start`, `on_update(dt)`, `on_fixed_update(dt)` | Fixed steps are 60 Hz, as in Godot |
| `queue_free()` | `e:destroy()` | |
| `get_node("Child")`, `$Child` | `e:child("Child")`, `noor.find("Name")` | |
| `add_to_group` / `get_nodes_in_group` | Entities have `tags` | **Not readable from scripts yet** (planned: gameplay foundations) |
| `@export var speed = 5` | `"script": {"params": {"speed": 5}}`, read as `self.params.speed` | Edited in the Inspector |

## Physics

| Godot | NoorEngine | Status |
|---|---|---|
| RigidBody2D / RigidBody3D | `rigidbody` (dynamic) + `collider`; in 2D add `"plane": "xy"` | ✅ ([2D physics](../decisions/ADR-040-2d-and-isometric.md)) |
| StaticBody2D | a `collider` with no rigidbody | ✅ |
| AnimatableBody (moved by code) | `rigidbody` `"type": "kinematic"`, moved from a script | ✅ |
| CollisionShape2D: rectangle, circle, convex polygon | `collider`: `rectangle`, `circle`, `polygon` | ✅ |
| **CharacterBody2D / 3D**: set `velocity`, call `move_and_slide()`, read `is_on_floor()`, `is_on_wall()` | `character` with `"motion": "script"`: set `e:set_velocity(v)`, it slides; read `e:is_on_floor()`, `e:is_on_wall()`, `e:is_on_ceiling()` | ✅ (2D part 2b). Or let NoorEngine handle gravity and jumping (`character_move`, `character_jump`), or use the ready-made `platformer` |
| One-way collision on a platform | `"one_way": true` on the collider | ✅ |
| **Area2D** (`body_entered`, `body_exited`) | a `collider` with `"trigger": true`; hooks `on_trigger_enter(other)`, `on_trigger_exit(other)` | ✅ |
| RayCast2D node | `noor.raycast(origin, direction, distance)` | ✅ (a function, not a node) |
| ShapeCast2D, `intersect_shape` | `noor.shape_cast(shape, from, to)`, `noor.overlap(shape, at)` | ✅ (2D part 2b) |

## Drawing, sound and effects

| Godot | NoorEngine | Status |
|---|---|---|
| Sprite2D, AnimatedSprite2D (SpriteFrames) | `sprite` + a `.noorsprite` sheet with animations | ✅ |
| `z_index`, `y_sort_enabled`, an unshaded material | the sprite's `layer` / `order`, `"sort": "y"`, `"unlit": true` | ✅ (2D part 3) |
| Camera2D (follow, smoothing, limits) | an orthographic `camera` | Follow, smoothing, limits and pixel-perfect: 2D part 6 |
| **TileMap**, TileSet, `set_cell` / `get_cell` | `tilemap` + a `.noortileset`; `e:set_tile(x, y, t)`, `e:tile(x, y)` ([Tilemaps](../fx/TILEMAPS.md)) | ✅ square, isometric, hexagonal (2D part 4); painted in the editor's 2D view (part 5) |
| ParallaxBackground | | 2D part 6 |
| GPUParticles2D | `emitter` + `.noorfx` (works in 2D and 3D) | ✅ |
| Light2D, normal-mapped 2D sprites | | Later (after the 2D foundations) |
| AudioStreamPlayer (2D/3D) | `audio_source`, `noor.play_sound` | ✅ |

## Game logic helpers

These are the ones a NoorEngine game misses most today. They're the **gameplay foundations** group, planned right
after the 2D foundations ([why that order](../milestones/M9_PLAN.md#the-order-from-here-sage-2026-11-20)).

| Godot | NoorEngine | Status |
|---|---|---|
| **Signals** between your own scripts (`signal hit`, `emit_signal`, `connect`) | Engine events arrive as hooks (`on_collision_enter`, `on_animation_end`...) | **Your own signals: not yet** (planned) |
| **Timer** node, `await get_tree().create_timer(1).timeout` | Compare `noor.time()` yourself | **Timers and waiting: not yet** (planned) |
| **Tween** | | **Not yet** (planned) |
| **AnimationPlayer**: animate any property (position, colour, a door opening), call functions on a timeline | Sprite animations and skeletal clips only | **Animating any property: not yet** (planned) |
| `preload("res://Bullet.tscn").instantiate()` | | **Spawning prefabs at runtime: not yet** (planned) |
| `get_tree().change_scene_to_file()` | | **Changing level: not yet** (planned) |
| Control, Label, Button, containers, themes | `noor.ui.text`, `noor.ui.rect` (a HUD drawn each frame) | **Real UI (menus, buttons, inventories): not yet** (planned) |
| FileAccess, ConfigFile, saving a game | | **Saving and loading: not yet** (planned) |
| NavigationAgent2D / 3D | | **Not yet** (planned) |
| Path2D / PathFollow2D | | Not yet (planned, with the spline tool) |
| Autoload singletons | A "Game" entity with a script in the scene | Works today, but no cross-scene state until levels can change |

## What's different on purpose

- **Units are metres**, even in 2D. A pixel-art sprite says how many pixels make a metre (`pixels_per_metre`, often
  16). Physics then behaves like the real world: gravity is 9.81 m/s², and a platformer simply turns it up
  (`gravity_scale`).
- **2D is the XY plane of a 3D world** (ADR-040): one physics engine, one renderer, one editor. 2D and 3D mix freely.
- **Built games are checked to play exactly like the editor** (to the bit), so what you test is what you ship.
