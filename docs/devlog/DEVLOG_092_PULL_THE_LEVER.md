# NoorEngine Devlog #092 — Pull the Lever

**Date:** 2026-11-21
**Build / commit:** Gameplay foundations, part 3: messages between scripts

## This week we actually did

**Scripts can talk to each other.** A lever and a door are the classic example. Until now the lever's script had to
find the door and poke its components, or the door had to watch the lever every frame. Now it's Godot's signals:

```lua
-- the lever says it
self.entity:emit("pulled", { strength = 3 })

-- the door listens
noor.find("Lever"):on("pulled", function(data, sender) ... end)
```

- **Messages from an entity** (`e:emit`, `e:on`) and **game-wide** ones (`noor.emit`, `noor.on`: "level complete",
  "player died").
- **Calling another script's function**: `door:call("open")`, like Godot's `door.open()`.
- **Lua and C# hear each other.** A Lua lever can open a C# door, and the door can call back into the lever.
- **Engine events are messages too.** A door can listen to a pressure plate's `trigger_enter` without the plate having
  any script at all.
- Delivery is **instant and in order**: when `emit` returns, everyone listening has already reacted. Listeners clean
  themselves up when either entity goes away.

## Build of the Week 🏆

**A Lua lever and a C# door.** The test project pulls a lever three times. The door counts the pulls, reads their
strength, asks the lever for its status, and finally the lever asks the door whether it's open. It runs once with a
Lua door and once with a C# door, in a project and in a built game, and all three runs must show the same HUD. They did,
first time, across both languages.

**No leaks.** A test spawns and destroys ten thousand listening entities, a hundred at a time, and checks that the
listener count goes back to zero every time.

## Sage vs. Reality

A quiet part: everything worked on the first build. The decisions took longer than the code:

- **Instant or queued?** Queued messages (delivered next frame) can't loop forever, but "when did the door open?"
  becomes a puzzle. Godot's signals are instant, and so is this, with a guard: a message that triggers itself 32 deep
  is stopped and reported.
- **What can cross languages?** Lua tables and C# objects don't mix, so message data travels as JSON. Entities inside
  the data arrive as their ids; the sender arrives as a real entity.

## Alden discovers ...

**... `noor.emit("dinner")`.** Alden at a campfire shouting "Dinner!"; every NPC on the hill turning round at once.
