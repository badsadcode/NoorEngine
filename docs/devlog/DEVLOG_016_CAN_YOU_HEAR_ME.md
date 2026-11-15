# NoorEngine Devlog #016 — Can You Hear Me Now?

**Date:** 2026-11-15
**Build / commit:** Milestone 4, part 5

## This week we actually did

**Sound.** Put a `.wav`, `.flac` or `.mp3` file into your project's `Assets` folder and it shows
up in the Content Browser with a little ♪. Click **Add to scene** and you get a sound emitter: an
entity that plays the sound when you press Play. Walk away from it and it gets quieter; turn your
head and it moves from one ear to the other.

Scripts can play sounds too, in both languages:

```lua
-- Lua: a one-off sound where something happened
noor.play_sound("Assets/Audio/thud.wav", self.entity:world_position())
```

```csharp
// C#: the same thing
Noor.PlaySound("Assets/Audio/thud.wav", Entity.WorldPosition);
```

Sounds go through **buses**: music, sound effects, interface, voice, and a master volume over
all of them, so a game can have a separate slider for each. The first-person player template now
has ears (on its camera), so it hears the world from where it stands.

## How it fits together (the short version)

We use **miniaudio**, one well-tested open-source file that talks to the sound card on Windows,
Linux and Mac and can read the common audio formats. NoorEngine tells it where the sounds and the
listener are every frame; miniaudio does the mixing.

## Build of the Week 🏆

**Tests that listen without speakers.** The build machines on GitHub have no sound card, and the
editor's self-test shouldn't beep at Tom at 3 a.m. So in tests, NoorEngine mixes the sound into
memory instead of sending it to speakers, and the tests *measure the numbers*:
- nothing playing → the mix is exactly silent;
- a sound 2 metres away is clearly louder than the same sound 40 metres away;
- turn the bus volume to zero → silence;
- a sound that played once tidies itself up afterwards.

The editor's self-test now writes a short beep file, adds it to a scene, and plays it with a C#
script: 109 checks.

What tests can't tell us is whether it *sounds* right through real speakers. That part needs
human ears.

## Sage vs. Reality

1. **The crash at the end.** Everything worked, then the tests crashed while *shutting down*.
   miniaudio wants each sound to be properly "unplugged" before its memory is freed; we were just
   freeing it. Now every sound is held by a small owner object that always unplugs first, so it
   can't be forgotten again.
2. **The script that started too early.** A script asked "is my sound playing?" in its very first
   moment, before the sound system had even loaded the sound. Fix: scripts now start *after* the
   engine's own systems have set everything up.

## Next engineering target
**Part 6: standalone builds.** A game that runs without the editor: `noor_player` and
`noor_cli build`.

## Bad Ideas Works Award for Questionable Decisions
**Winner:** Spending an afternoon teaching a game engine to make noise, then making sure all its
tests are completely silent.
**Defence:** A sound you can measure is a sound you can trust. A sound at 3 a.m. is a sound you
can regret.

---
*Bad Ideas Works — Code shall remain understandable to whichever AI inherits this mess.*
