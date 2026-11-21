# NoorEngine C# API

Status: **Milestone 4, parts 4–7**. The same API as Lua ([LUA_API.md](LUA_API.md)), in C#.
Everything here is implemented and tested (`tests/unit/test_csharp_host.cpp` for the engine half,
the editor smoke test with real scripts).

## Attaching a script

In the Inspector: **Add component ▸ C# script…** creates `Scripts/<Name>.cs` from a template,
plus `Scripts/Scripts.csproj` (an ordinary .NET project: open it in Visual Studio, Rider or
VS Code for completion and debugging). The class name must match the file name.

```json
"script": { "file": "Scripts/Spinner.cs", "params": { "speed": 90 } }
```

**Play compiles the scripts** (`dotnet build`, about a second; needs the .NET 10 SDK). Compile
errors appear in the Console as `Scripts/Spinner.cs:7: CS1002: ; expected` and Play doesn't start.
Stop unloads the scripts, so edit and press Play again.

## A script

```csharp
using System.Numerics;
using NoorEngine;

public class Spinner : NoorScript
{
    public override void OnStart() => Noor.Log($"spinning {Entity.Name}");

    public override void OnUpdate(float dt)
    {
        Entity.Rotate(Vector3.UnitY, Param("speed", 90) * dt);
    }
}
```

Each entity gets its own instance. `Entity` is the entity it runs on, `Params` the component's
params (`JsonObject`); `Param(name, fallback)` reads a number.

| Hook | When |
|---|---|
| `OnStart()` | Once, when play starts (or the entity appears) |
| `OnUpdate(float dt)` | Every frame |
| `OnFixedUpdate(float dt)` | At a fixed 60 Hz, before physics |
| `OnLateUpdate(float dt)` | Every frame, after the fixed steps |
| `OnDestroy()` | When the entity is destroyed, or play stops |
| `OnCollisionEnter/Exit(Entity other)`, `OnTriggerEnter/Exit(Entity other)` | Physics contacts and triggers |
| `OnAnimationEnd(string animation)` | A non-looping animation has finished: a sprite's or a model clip this script played, or a controller state (any layer) |
| `OnAnimationEvent(string name)` | A named moment of the playing animation was passed (footsteps...) |

An exception stops that script for that entity (the Console shows the exception, the hook and the
line); the game keeps running.

## `Noor` and `Input`

| | |
|---|---|
| `Noor.Log(msg)`, `Noor.Warn(msg)`, `Noor.Error(msg)` | Console |
| `Noor.Time` | Seconds since play started |
| `Noor.Find(name)` | `Entity?` |
| `Noor.FindAll(tag)` | `Entity[]`: every entity with the tag, in scene order (a group; rt API v13) |
| `Noor.Spawn(prefab, position, euler = null, parent = null)` | `Entity?`: a new copy of a prefab (asset id or `Assets/...noorprefab` path), its root; it starts next frame (rt API v13) |
| `Noor.Raycast(origin, direction, maxDistance = 1000, ignore = null)` | `RaycastHit?` (`Entity`, `Point`, `Normal`, `Distance`) |
| `Noor.ShapeCast(shapeJson, from, to, ignore = null)`, `Noor.Overlap(shapeJson, at, ignore = null)` | A swept shape's first hit; the entities a shape overlaps (rt API v11). Shapes as collider JSON, e.g. `{"shape":"circle","radius":0.3}` |
| `Water.Height(x, z)` | `float?`: the water surface's height (world y) at world (x, z) now, waves included; null where there's no water (8D) |
| `Water.Flow(x, z)` | `Vector3?`: the water's own velocity there (m/s: a river's current), null where there's no water (8D, rt API v9) |
| `Water.Normal(x, z)` | `Vector3?`: its normal there |
| `Noor.PlaySound(clip, position = null, volume = 1, bus = "sfx")` | A one-shot sound (asset id or `Assets/...` path); 3D with a position. `true` if it started |
| `Noor.SetVolume(bus, volume)` | Bus volume: `master`, `music`, `sfx`, `ui`, `voice` |
| `Input.Down/Pressed/Released(action)` | Actions from `Settings/input.json` (`jump`, `fire`, …) |
| `Input.Axis(name)` | `move_x`, `move_y` (−1..1), `look_x`, `look_y` (mouse) |
| `Input.CaptureMouse(bool)`, `Input.MouseCaptured` | Mouse capture for looking; Esc releases |

## `Entity`

A handle to an entity (a class, equal when it's the same entity): `Entity.Position = v`,
`hit.Entity.Position = v` and `Noor.Find("Door")?.Destroy()` all work. Keep entities in fields,
lists and dictionaries as you like.

Vectors are `System.Numerics.Vector3`; angles in degrees; transforms local unless named World.

| | |
|---|---|
| `Name`, `IdText`, `Exists`, `Parent`, `Child(name)`, `Bone(name)`, `Destroy()` | Identity; `Bone` finds a model's bone anywhere below |
| `Teleport(position, euler = null)` | At once, through physics for characters and bodies (velocity and jumps reset): a respawn (rt API v13) |
| `HasTag(tag)`, `AddTag(tag)`, `RemoveTag(tag)` | Groups (rt API v13) |
| `Position`, `Euler`, `Scale` (get/set) | Local transform |
| `Translate(v)`, `TranslateLocal(v)`, `Rotate(axis, degrees)` | Moving |
| `WorldPosition`, `Forward`, `Right`, `Up` | World space |
| `Get(component)` → `JsonNode?`, `Set(component, JsonNode?)` | Component data (copies) |
| `Velocity` (get/set), `AddImpulse(v)`, `AddForce(v)` | Physics bodies; on a character, its velocity (with `"motion": "script"` the script owns it) |
| `IsOnFloor`, `IsOnWall`, `IsOnCeiling`, `FloorNormal` | Character: what it touched in its last step |
| `Tile(x, y)`, `SetTile(x, y, tile)` | A tilemap's cell (rt API v12) |
| `CharacterMove(v)`, `CharacterJump(speed)`, `IsGrounded` | Character controller |
| `CharacterJumpRelease()`, `CharacterDrop()` | The jump button let go (a shorter jump); fall through a one-way platform (rt API v10) |
| `WaterState`, `FeetDepth`, `HeadDepth` | Character controller in water (8D, rt API v8): "dry", "wading", "swimming", "under" (null: not a character); the water's depth at its feet and head |
| `PlaySound()`, `StopSound()`, `IsPlaying` | The entity's `audio_source` ([components](LUA_API.md#sound-components)) |
| `PlayAnimation(name, speed = 1)`, `StopAnimation()`, `SetAnimationSpeed(s)`, `Animation` | The entity's `animator` (a clip, or a controller's base-layer state) or else its `sprite` ([animation](../animation/ANIMATION.md), [sprites](../fx/SPRITES.md)); a non-looping animation calls `OnAnimationEnd` |
| `CrossFade(name, seconds)`, `SetAnimationParameter(name, float or bool)`, `SetAnimationTrigger(name)` | An `animator`: blend to a clip or state over `seconds`; set its controller's parameters (a trigger resets when a transition uses it) |
| `Burst(count)` | The entity's `emitter`: `count` particles at the next fixed step |

## Messages between scripts

Gameplay foundations, part 3 (rt API v15); the same as Lua's, and the two hear each other:

```csharp
Entity.Emit("pulled", new JsonObject { ["strength"] = 3 });          // this entity says it
On(Noor.Find("Lever")!, "pulled", (data, sender) => { /* ... */ });  // listen to another
On("level_complete", (data, _) => { /* ... */ });                    // game-wide; Noor.Emit sends one
var open = Noor.Find("Door")!.Call("is_open", "please");             // a function of its script (Lua or C#)
```

`Call` finds a method of the script by name (public or not) and fills its parameters from the arguments (JSON
converted to their types); its return value comes back as a `JsonNode`. A script's own methods only, not
`NoorScript`'s.

## Time: timers, waiting, pause

The same rules as Lua's (gameplay foundations, part 2; rt API v14):

```csharp
public override async void OnStart()
{
    After(2, () => Door.Destroy());               // once, two seconds from now
    var blink = Every(0.5, () => _lampOn = !_lampOn);
    Every(1, () => ++_hp < 100);                  // returning false stops it
    await Wait(1);                                // inside any async method (async void hooks too)
    Entity.PlayAnimation("open");
}
```

| | |
|---|---|
| `After(seconds, action, real = false)`, `Every(seconds, action, real = false)` | Timers (`timer.Cancel()`); gone with the entity |
| `await Wait(seconds, real = false)` | Carries on that much later (on the main thread, before OnUpdate) |
| `Noor.Paused` (get/set), `Noor.TimeScale` (get/set), `Noor.RealTime` | Pause, slow motion, the real clock. Scripts with **Runs while paused** keep running while paused |

## `Ui` (the HUD)

```csharp
public override void OnUpdate(float dt)
{
    Ui.Text($"Score: {_score}", 20, 20, size: 32);
    Ui.Rect(0, 0, 4, 4, "#FFFFFFC0", "center");  // a crosshair
}
```

| | |
|---|---|
| `Ui.Text(text, x, y, size = 24, color = "#FFFFFF", anchor = "top_left")` | Text (line height `size`; `\n` for new lines) |
| `Ui.Rect(x, y, width, height, color = "#FFFFFF", anchor = "top_left")` | A filled rectangle |

Same rules as Lua ([The HUD](LUA_API.md#the-hud)): draw every frame; UI units (the screen is 720
tall); anchors `top_left` … `bottom_right`; colors `"#RRGGBB[AA]"`. A wrong color or anchor throws
`ArgumentException`.

## In built games

File ▸ Build Game (or `noor_cli build`) compiles your scripts into the game's `managed/` folder;
a compile error stops the build and names the file and line. The game starts .NET itself, so the PC
playing it needs the [.NET 10 runtime](https://dotnet.microsoft.com/download/dotnet/10.0) (not the
SDK). Without it the game still runs, and its log (`<Game>.log` next to the .exe) says that C# scripts are
off and why.

## How it works (for engine people)
The engine calls scripts through `noor_rt_host` and scripts call the engine through `noor_rt_api`
(`engine/scripting/include/noor_rt_api.h`): two tables of C function pointers, so the same managed
library (`engine/scripting/dotnet/NoorEngine.Scripting`) works in the editor and, later, the player.
Scripts load into a collectible `AssemblyLoadContext` (from memory, so the next build can overwrite
the DLL) and are unloaded on Stop.
