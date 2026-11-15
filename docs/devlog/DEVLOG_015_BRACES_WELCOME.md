# NoorEngine Devlog #015 — Braces Welcome

**Date:** 2026-11-15
**Build / commit:** Milestone 4, part 4

## This week we actually did

**C# scripts.** NoorEngine now speaks two scripting languages: Lua and C#, with the same
abilities in both. In the Inspector, **Add component ▸ C# script…**, type a name, and you get a
file like this:

```csharp
public class Spinner : NoorScript
{
    public override void OnUpdate(float dt)
    {
        Entity.Rotate(Vector3.UnitY, Param("speed", 90) * dt);
    }
}
```

Press **Play**. NoorEngine compiles your scripts first (about a second) and then runs them. If
there's a mistake, the Console says exactly where (`Scripts/Spinner.cs:7: ; expected`) and the game
doesn't start. If a script crashes while playing, the Console names the line, that one script
switches off, and everything else keeps going. Edit, press Play again, and the new version runs.

The scripts folder is a normal .NET project, so Visual Studio, Rider or VS Code open it with
full autocomplete. You don't need NoorEngine to be open for that.

## How it fits together (the short version)

The engine is C++. Your scripts are C#. Between them sit two small "phone books" of functions:
one lists what scripts may ask the engine to do (move this, find that, play nicely), the other
lists what the engine may ask the scripts (start, update, you bumped into something). Lua
uses the same list of engine functions, so both languages can always do the same things.

## Build of the Week 🏆

The editor's self-test now writes C# on purpose, badly:
- a working spinner: it turns 87° in about a second at 90° per second;
- a missing semicolon: the Console points at line 7 and Play refuses to start;
- a script that throws an exception: reported with its line, the game keeps running;
- then it fixes the code, presses Play, and checks that the *new* version is the one running
  (it spins the other way).

Errors provoked on purpose are counted separately, so "zero errors" at the end still means zero
real ones. 103 checks.

## Sage vs. Reality

The compile errors kept arriving with a long project path stuck to the end. The cause was one
invisible character: Windows ends lines with two characters, and the pattern that cleans up the
messages only expected one. Then, while fixing it, an over-helpful shell turned the `\r` into
that same invisible character *inside the source code*. Fixed by writing the fix to a file first
and running that instead.

## Next engineering target
**Part 5: sound.** Footsteps, one-shots and music, in 3D.

## Bad Ideas Works Award for Questionable Decisions
**Winner:** A test that writes broken code on purpose to make sure the editor complains properly.
**Defence:** Every programmer does this. We just admit it's deliberate.

---
*Bad Ideas Works — Code shall remain understandable to whichever AI inherits this mess.*
