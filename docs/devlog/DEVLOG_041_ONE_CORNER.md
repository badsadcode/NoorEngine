# NoorEngine Devlog #041 — One Corner

**Date:** 2026-11-18
**Build / commit:** Milestone 7, part 3

## This week we actually did

**Terrains can be built from procedural graphs now, and graphs know which part of the world changed.**

- **Tiles are regions.** Terrains come in tiles of 256 metres, and now graphs think in the same tiles.
  Most nodes work tile by tile and remember tile by tile. Move a hill in one corner of a four-tile
  terrain and exactly one tile is recomputed, in the hill node and in everything after it. The
  other three come out bit for bit the same, and the test checks every sample.
- **Five new terrain nodes:** a **stamp** (a hill or a plateau, or a hollow with a negative height),
  **terrace** (steps), **height band** (a mask for "between 10 and 20 metres"), **slope** (a mask for
  steep ground) and **heightmap** (heights from a picture in the project).
- **A terrain whose base is a graph.** Point a terrain at a graph and it uses the graph's heights.
  Sculpting and paint sit on top, as they do on any base. Change the graph's seed and the hills
  underneath change while your sculpted bump stays put, now on the new ground. The test measures it.
- **Games don't run graphs.** The editor "cooks" the graph into a small file when it changes and on
  every save, and `noor_cli build` cooks it into the game. A built game's walker, dropped from 30
  metres, lands on the graph's terrain 2 mm from where the maths says the ground is.

No pretty picture: the test terrain is white hills with a yellow dot on top. The forest comes later.

## Build of the Week 🏆

**The fingerprint that didn't move.** Turning six nodes from "do the whole field" into "do it tile
by tile" is the kind of change that quietly breaks things. The pinned fingerprint from part 1 came
out exactly the same afterwards: the tiled version makes the same bytes as the old one.

## Sage vs. Reality

**The script that ate a file.** One of my patch scripts ended with a line that opened `terrain.cpp`
for writing before reading it back, and "opening for writing" empties a file first. The terrain
code was briefly zero lines long. Git had the committed version, so I restored it, re-applied the
change properly and checked the line count, all before anything was built or committed.

**Tiles that leave the stamp alone.** A stamp knows its circle. If the circle doesn't reach a tile,
that tile isn't just "unchanged": it's handed on under the *input's* name. That's what stops a change
in one corner from rippling through the rest of the graph.

## Next engineering target
**Part 4: the graph editor.** Nodes on a canvas, wires between them, settings in the Inspector, Run
with progress and Cancel. Graphs stop being something you edit as text.

## Bad Ideas Works Award for Questionable Decisions
**Winner:** Dropping a capsule from 30 metres to check whether a file exists.
**Defence:** If the file had been missing, it would still be falling. That's a very clear error message.
