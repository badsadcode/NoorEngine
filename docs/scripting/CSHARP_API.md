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

An exception stops that script for that entity (the Console shows the exception, the hook and the
line); the game keeps running.

## `Noor` and `Input`

| | |
|---|---|
| `Noor.Log(msg)`, `Noor.Warn(msg)`, `Noor.Error(msg)` | Console |
| `Noor.Time` | Seconds since play started |
| `Noor.Find(name)` | `Entity?` |
| `Noor.Raycast(origin, direction, maxDistance = 1000, ignore = null)` | `RaycastHit?` (`Entity`, `Point`, `Normal`, `Distance`) |
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
| `Name`, `IdText`, `Exists`, `Parent`, `Child(name)`, `Destroy()` | Identity |
| `Position`, `Euler`, `Scale` (get/set) | Local transform |
| `Translate(v)`, `TranslateLocal(v)`, `Rotate(axis, degrees)` | Moving |
| `WorldPosition`, `Forward`, `Right`, `Up` | World space |
| `Get(component)` → `JsonNode?`, `Set(component, JsonNode?)` | Component data (copies) |
| `Velocity` (get/set), `AddImpulse(v)`, `AddForce(v)` | Physics bodies |
| `CharacterMove(v)`, `CharacterJump(speed)`, `IsGrounded` | Character controller |
| `PlaySound()`, `StopSound()`, `IsPlaying` | The entity's `audio_source` ([components](LUA_API.md#sound-components)) |
| `PlayAnimation(name, speed = 1)`, `StopAnimation()`, `SetAnimationSpeed(s)`, `Animation` | The entity's `animator` (a clip, or a controller's base-layer state) or else its `sprite` ([animation](../animation/ANIMATION.md), [sprites](../fx/SPRITES.md)); a non-looping animation calls `OnAnimationEnd` |
| `CrossFade(name, seconds)`, `SetAnimationParameter(name, float or bool)`, `SetAnimationTrigger(name)` | An `animator`: blend to a clip or state over `seconds`; set its controller's parameters (a trigger resets when a transition uses it) |
| `Burst(count)` | The entity's `emitter`: `count` particles at the next fixed step |

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
