# Input: keys, gamepads, players, rebinding

Scripts ask for **actions** ("jump") and **axes** ("move_x"), never for keys, so the same game works with a keyboard, a
mouse and gamepads, and players can change what does what.

## Actions and axes

Every game starts with these (a project's `Settings/input.json` changes or adds to them):

| Name | Keyboard and mouse | Gamepad |
|---|---|---|
| `move_x` (axis) | A / D, Left / Right | left stick, d-pad |
| `move_y` (axis, + is forward / up) | S / W, Down / Up | left stick, d-pad |
| `look_x`, `look_y` (axes) | the mouse's movement in pixels | none (see `look_stick_x`, `look_stick_y`) |
| `look_stick_x`, `look_stick_y` (axes) | none | right stick, −1..1 (multiply by your own turn speed) |
| `jump` | Space | South (Xbox A, PlayStation ✕) |
| `sprint` | Shift | left stick press |
| `crouch` | Ctrl, C | East (Xbox B, PlayStation ○) |
| `fire` | left mouse button | right trigger |
| `aim` | right mouse button | left trigger |
| `interact` | E | West (Xbox X, PlayStation □) |
| `pause` | Escape | Start |

```lua
if noor.input.pressed("jump") then ... end
local x = noor.input.axis("move_x")   -- -1..1: keys whole steps, a stick smooth
```

## Settings/input.json

```json
{
  "actions": { "jump": ["Space", "Pad.South"], "dash": ["Q", "Pad.RB"] },
  "axes": { "turn": { "negative": ["Q", "Pad.RightX-"], "positive": ["E", "Pad.RightX+"] } },
  "dead_zone": 0.2,
  "press": 0.5
}
```

An entry replaces the built-in one of the same name; others stay. **Names:** keys as on the key (`A`, `7`, `Space`,
`Enter`, `Escape`, `Tab`, `Shift`, `Ctrl`, `Alt`, `Up`..`Right`, `F1`..`F12`, `MouseLeft`, `MouseRight`, `MouseMiddle`);
pad buttons by where they are: `Pad.South`, `Pad.East`, `Pad.West`, `Pad.North` (or an Xbox pad's `Pad.A`, `Pad.B`,
`Pad.X`, `Pad.Y`), `Pad.Start`, `Pad.Back`, `Pad.LB`, `Pad.RB`, `Pad.LS`, `Pad.RS` (stick presses), `Pad.Up`..`Pad.Right`
(the d-pad); sticks one direction at a time: `Pad.LeftX+`, `Pad.LeftX-`, `Pad.LeftY+` (down), `Pad.LeftY-` (up),
`Pad.RightX+`...; triggers `Pad.LT`, `Pad.RT`. An unknown name is an error that names it.

**Sticks** have a round dead zone (`dead_zone`, a fraction of the stick's travel; 0.2 by default): inside it they read
0, past it they rise smoothly to 1. A stick or trigger counts as a press (for actions) past `press` (0.5).

## Gamepads and players

Xbox, PlayStation, Switch and most other pads work the same way (SDL's gamepad mappings); plug them in or out any time.
Up to **four players**: player 1 is the keyboard and mouse together with the first pad; players 2 to 4 are the next
pads. A pad unplugged and plugged back in gets its old player back.

```lua
if noor.input.pressed("jump", 2) then ... end           -- player 2's pad
local x = noor.input.axis("move_x", 2)
noor.on("pad_disconnected", function(d) noor.set_paused(true) end)   -- d.player
noor.on("pad_connected", function(d) noor.log("player " .. d.player .. ": " .. d.name) end)
for _, p in ipairs(noor.input.pads()) do ... end         -- {player, name, connected}, all four
```

Menus ([Game UI](../ui/GAME_UI.md)) follow the first pad too: the d-pad or left stick moves focus, South presses, East
is Escape.

## Rebinding

A settings menu lets players change bindings; what they change is theirs, saved apart from the game:

```lua
-- "Press a key for Jump": wait for something to be pressed, then use it.
local pressed = noor.input.last_pressed()          -- "J", "Pad.North", "Pad.RightX+"... or nil
if pressed then
    noor.input.rebind("jump", "Space", pressed)    -- replace Space with it (nil old: add; nil new: remove)
    noor.input.save_bindings()
end
noor.input.bindings("jump")                         -- {"J", "Pad.South"}; an axis side: "move_x+", "move_x-"
noor.input.reset_bindings("jump")                   -- the game's own back (no name: everything)
```

A control does one thing: rebinding something already used elsewhere moves it. `save_bindings()` writes only what
differs from the game's own map to `input.json` in the player's folder (`%APPDATA%\NoorEngine\<game>` on Windows),
read over the game's map at the next start; a broken file there is ignored with a warning, never a crash.

| Lua | C# |
|---|---|
| `noor.input.down(action [, player])`, `pressed`, `released` | `Input.Down(action, player = 1)`, `Pressed`, `Released` |
| `noor.input.axis(name [, player])` | `Input.Axis(name, player = 1)` |
| `noor.input.pads()` | `Input.Pads()` → `(Player, Name, Connected)[]` |
| `noor.input.bindings(name)` | `Input.Bindings(name)` → `string[]` |
| `noor.input.rebind(name, old, new)` | `Input.Rebind(name, old, newBinding)` |
| `noor.input.reset_bindings([name])` | `Input.ResetBindings(name = null)` |
| `noor.input.save_bindings()` | `Input.SaveBindings()` |
| `noor.input.last_pressed([player])` | `Input.LastPressed(player = 1)` |
| `noor.on("pad_connected"/"pad_disconnected", fn)` | `On("pad_connected", (data, _) => ...)` |

## Not yet

Pads in the **editor's Play** (the editor's viewport has no SDL; pads work in noor_player and built games), rumble, a
pad's touchpad, gyro and lights, and on-screen button pictures (show "A" or "✕" by the pad's make). **Testing:**
`noor_player --pad-script <file.json>` plays a simulated pad from a list of events (plugged in, buttons pressed, sticks
moved, unplugged); `--user-dir <folder>` puts the player's settings somewhere of a test's own.
