# NoorEngine Devlog #018 — Packing the Braces

**Date:** 2026-11-16
**Build / commit:** Milestone 4, part 6b

## This week we actually did

**C# scripts now travel with the game.** Yesterday's builds left C# scripts at home: Lua went
along, C# didn't. Now **File ▸ Build Game…** compiles your C# scripts and packs them into the game,
and the game starts .NET by itself when it runs.

```
My Game/
  My Game.exe
  game.json
  game/        scenes, models, sounds, Lua scripts
  managed/     your compiled C# scripts, and the engine's C# half
```

If a script has a mistake, the build stops before it touches the folder and the Console says
where, exactly like Play does: `Scripts/Spinner.cs:7: CS1002: ; expected`.

Also new: double-clicking a game no longer opens a black console window next to it. The game is
now a proper windowed program, and its log goes into a file next to the .exe (`My Game.log`), in
case something needs explaining.

## How it fits together (the short version)

The editor is itself a .NET program, so it can run C# scripts directly. The player is plain C++,
so it has to *start* .NET first. Microsoft provides an official way to do that (a small library
called **hostfxr** that comes with every .NET installation). The player finds it, starts .NET,
loads the engine's C# half, and then calls one door into it: "here are the engine's functions,
here are the game's scripts, give me back the table to run them with". From then on, a C# script
in a built game works exactly as it does in the editor.

The PC that plays the game needs the free .NET 10 runtime (not the developer kit). If it's
missing, the game still starts, and its log says C# is off and where to download .NET.

## Build of the Week 🏆

The test game has a yellow box lifted by a C# script. The test builds the game, runs the *built*
copy for 120 frames, and checks the yellow box rose exactly 2 metres. Another test plants a broken
script and checks that the build refuses, names `Broken.cs` line 4, and leaves the output folder
untouched.

And a bonus check: the editor's whole self-test project (a player, physics, Lua, C#, sound) built
into a game and ran for three seconds without a single error.

## Sage vs. Reality

1. **The scripts couldn't find their own engine.** The first built game started .NET, then
   failed with "can't find NoorEngine.Scripting", the very library that had just loaded it. .NET
   keeps libraries in separate rooms ("load contexts"), and the player's way of starting .NET put
   the engine's C# half in a different room from the scripts. Fix: the scripts' room now points
   straight at the engine's copy, whichever room it's in.
2. **A copy that never refreshed.** Yesterday's build tool keeps a copy of the player next to
   itself, and I noticed it only refreshed when the tool itself changed. So a fixed player would
   have kept shipping the old one. Fixed before it bit anyone.
3. **Half a build is worse than none.** If C# failed to compile, the output folder was left
   half-written and the next build refused to use it. Now the scripts compile first, in a scratch
   folder, and nothing is written until they do.

## Next engineering target
**Part 7: NoorCode MVP and a game HUD.** Edit scripts inside NoorEditor, and put text and shapes
on the game's screen.

## Bad Ideas Works Award for Questionable Decisions
**Winner:** A C++ program that starts a .NET runtime so that a C# script can call back into the
same C++ program to move a box.
**Defence:** That's not a bad idea, that's an *architecture*. (It is also how the big engines do
it.)

---
*Bad Ideas Works — Code shall remain understandable to whichever AI inherits this mess.*
