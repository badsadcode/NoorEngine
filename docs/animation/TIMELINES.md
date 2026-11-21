# Timelines: animate anything

Gameplay foundations, part 5 (Godot's AnimationPlayer). A **timeline** is keyframes over time: a door's hinge swinging
open, a light fading up, a platform's route, a cutscene's camera, plus things that happen at moments (a script function
called, a message sent). Skeletal clips (`ANIMATION.md`) move a model's bones; timelines move everything else.

## Making one in the editor

1. Select the entity to animate (a door) and open the **Timeline** tab at the bottom.
2. **New timeline**, and give it a name (`open`): a `.noortimeline` file in `Assets/Timelines/`, and the entity gets an
   **animation player** with it (the first one autoplays when the game starts; change `autoplay` in the Inspector).
3. Pick what to key in the **Key** box: the entity itself, or one of its children (`Hinge`). Pose it in the scene,
   drag the scrub to a time, and press **Key position**, **Key rotation** or **Key scale**: what the scene shows goes
   in as a key. **Key property…** keys any number or colour in a component (`light.intensity`, `sprite.color`). The
   easing box shapes the way into the next key.
4. Drag the scrub, or **Play**: the viewport shows the timeline without changing the scene (**Stop** puts everything
   back, and so does any edit, undo, save or Play).
5. **Add call…** calls a script function when play passes that time. Click a key to select it; **Delete key**.

Key edits are saved to the timeline file at once; they aren't undo steps (the entity's animation player is).

## The file

```json
{"noor_timeline": 1, "name": "DoorOpen", "length": 1.5, "loop": false,
 "tracks": [
   {"target": "Hinge", "property": "euler", "keys": [{"t": 0, "v": [0, 0, 0]}, {"t": 1.0, "v": [0, 100, 0], "ease": "back_out"}]},
   {"target": "", "property": "light.intensity", "keys": [{"t": 0, "v": 0}, {"t": 1.5, "v": 600}]},
   {"target": "", "call": true, "keys": [{"t": 0.5, "function": "creak", "args": [2]}]},
   {"target": "Hinge", "emit": true, "keys": [{"t": 1.0, "message": "latched", "data": {"how": "click"}}]}]}
```

- `target`: a path from the entity playing it: `""` itself, `"Hinge"` a child, `"Arm/Hand"` deeper. One timeline can
  move a whole rig of children.
- Value tracks' `property`: anything a tween moves (`position`, `euler`, `rotation`, `scale`, `world_position`,
  `"component.field"`). A key's `ease` is the way *into* it (the 22 easings of [tweens](../scripting/LUA_API.md#tweens-smooth-changes-in-one-line)).
- `call` keys call a function of the target's script (Lua or C#) with `args`; `emit` keys send a message from the target.
  Each fires once as play passes it (a key at 0 fires when play starts).

## The animation player

```json
"animation_player": {"timelines": {"open": "<id>", "close": "<id>"}, "autoplay": "open"}
```

While it plays it also holds `"playing"`, `"time"` and `"speed"`. From scripts:

```lua
door:play_timeline("close")            -- from the start (optionally a speed: door:play_timeline("close", 2))
door:stop_timeline()                   -- stops where it is
door:timeline()                        -- what it's playing, or nil
door:on("timeline_end", function(data) noor.log(data.name .. " finished") end)
```

C#: `Entity.PlayTimeline(name, speed)`, `StopTimeline()`, `Entity.Timeline`. A timeline that doesn't loop ends with the
message `timeline_end` (`{name}`) from its entity. Timelines run on the game clock: they pause with the game.

## Not yet

Dragging keys along the track and a curve editor; blending from one timeline into another; recording keys
automatically as you move things; scrubbing doesn't fire call and emit keys (only playing the game does); undo for key
edits.
