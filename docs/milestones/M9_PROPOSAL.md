# Milestone 9: a proposal (not a plan yet)

**Status:** answered by Tom and Noor (2026-11-20); the plan is [M9_PLAN.md](M9_PLAN.md) · **Date:** 2026-11-20 · **From:** Sage, after the
[Milestone 8D review](M8D_REVIEW.md) closed Milestone 8.

## What the roadmap says

Milestone 9 is "Roads, architecture, cities, vegetation ecology and simulation" (spec, Milestone 9): **deep modular
procedural world building**. Deliverables: a road graph, road and terrain modifiers, building footprints and rooms,
parcel and city generation, vegetation placement and simulation interfaces, and GIS as a data pipeline. Its gate:

1. **One change to an input** (moving a road, say) **reliably updates everything that depends on it and protects
   manual overrides**;
2. **the whole workflow can be automated headlessly**;
3. **a streamed world demo loads without corrupt references**.

The spec's example pipeline (15.2) is a terrain at the root, with roads, water, cities, buildings, vegetation and
simulation hanging off it, all feeding "streamable world cells". Its acceptance story is "draw a road, the terrain
changes, parcels and buildings follow, undo and regenerate" (31.2). Its showcase is NoorWorldShowcase (24, example D):
"an explorable landscape with terrain, a procedural forest, rocks, a road, a small village, a lake or river, sky and
weather, with the generator graphs, manual overrides and a streaming inspector".

As with Milestone 8, that's too much for one milestone done well. **I propose groups, each ending in its own review.**

## Where I'd push back on the roadmap

1. **Streaming is in the gate, but nowhere in the deliverables.** "A streamed world demo" needs large-world coordinates
   (ADR-013, still open), loading and unloading parts of a world without stalls, and a cheaper way to gather what's on
   screen. Milestone 8 measured that last one: gathering walks every entity every frame (8C: 250 characters 9 ms of
   CPU). This has to come **first**, not last: roads and villages over a world too big for memory are what the gate
   asks for.
2. **"Modular" has a studio meaning.** NoorArchitect (BadBuilding), NoorRoads and the other generators live in their
   own repositories and connect through **files** (Noor Interchange: GLB, a manifest, a recipe with a seed), not
   through each other's code. So Milestone 9's engine work is the **host**: the road graph and its editing, terrain
   modifiers, parcels, the masks that keep forests off roads, and calling a generator headlessly with a recipe and a
   seed and importing what it makes. That decides ADR-007 (the plugin ABI, still open): **a generator is a process
   with a file contract** first, in-process plugins later if ever.
3. **"Simulation interfaces" is vague.** Traffic? Ecology growing over years? People living in the village? Each is a
   milestone of its own. I'd keep Milestone 9 to an **interface**: a stage that takes the world's data and returns
   changes (a forest growing ten years), with one small example. The rest waits for a game that needs it.
4. **Generation itself isn't listed, and Thornwake shows it should be.** Roads, villages and forests need land worth
   building on: landforms, drainage, lakes at their spill level, rivers, biomes. Thornwake's world generator
   (`F:\Codex\ThornwakeNEW\Game\WorldGen`, ~300 decisions) does exactly this and has paid for the lessons: water carved
   last ("water always wins"), world types as data recipes, fingerprint tests, rivers as reaches with a falling surface
   and a width (which is precisely 8D's river component), vegetation as populations. I'd **port its ideas, not its
   C#**, as nodes in Milestone 7's procedural graph.
5. **GIS is a separate pipeline.** DEMs, GeoTIFF, coordinate systems, OpenStreetMap roads and footprints (ODbL
   attribution). It's what Baltic Drive does, but Baltic Drive stays on Godot (spec 2.3). Valuable, but no NoorEngine
   game needs it yet; I'd make it the last group and let Tom decide whether it stays in 9.

## Proposed groups, in this order

| Group | What | Why here |
|---|---|---|
| **9A: Big worlds** | Large-world coordinates (ADR-013: Baltic Drive's moving origin as the reference), world cells that load and unload on a budget without stalls, a cached scene gather (only what moved), a streaming inspector; a streamed demo world that loads without corrupt references | The gate's third line; everything after it builds worlds bigger than memory. Fixes 8C's crowd exception on the way |
| **9B: Land and water** | Landform, drainage and climate stages as procedural graph nodes (Thornwake's lessons, our code): lakes filled to their spill level and rivers as reaches, both emitted as 8D water bodies; biome maps for 9E; world types as recipes | Roads and villages need land shaped by water; this is where Thornwake's work lands |
| **9C: Roads** | A road graph asset and an editor tool (draw, branch, junctions), road meshes and decals, terrain modifiers (cut, fill, flatten) in a protected layer, exclusion masks for scatter; the same spline tool edits 8D rivers (8D's missing spline tool); bridges where roads cross water (placeholders until NoorBridges) | The spec's first named deliverable and its acceptance story's first step |
| **9D: Settlements and buildings** | Blocks and parcels from the road graph, building footprints, NoorArchitect called headlessly with a recipe and seed (ADR-007 as a process contract), its GLB, manifest, rooms and openings imported through Noor Interchange; regeneration when a footprint changes, hand-placed buildings kept | "Draw a road, parcels and buildings follow" |
| **9E: Vegetation ecology and simulation** | Species preferences (biome, moisture, slope, elevation, light), layers (canopy to groundcover), urban rules (road buffers, building clearance) from 9C/9D's masks, populations (Thornwake's approach), protected manual painting; the simulation **interface** with one example (a forest growing over years) | Forests that know about roads and villages; needs 9B's biomes |
| **9F: GIS (optional)** | DEM and GeoTIFF import, explicit coordinate systems and datums, OpenStreetMap roads and footprints into 9C/9D, attribution collected from manifests | A data pipeline; Tom's call whether it belongs in 9 |
| **The showcase** | NoorWorldShowcase: a valley with terrain, forest, rocks, a road, a small village, a lake and river, weather; graphs and overrides open; streaming inspector. Alden lives there | The milestone's sample and its gate review |

Each group would start, as 8A–8D did, with its own plan, ADR and parts, each part tested, documented and pushed.

## What should decide the order

1. **The gate** asks for streaming and dependable regeneration: 9A first, and every later group proves "change an input,
   dependants follow, overrides survive".
2. **The games.** The Uncounted, Chapter Two (sprites, zones, villages?) is the first planned NoorEngine game; Thalassa
   (deep sea) would want ocean-floor generation more than roads; Thornwake's own worlds are big and generated. Which
   one Milestone 9 should serve first changes the order of 9B–9E.
3. **What's already there:** Milestone 7's procedural graph (fields, caching, overrides, scatter), terrain with sculpt
   and paint layers, 8D's lakes and rivers, Noor Interchange. Most groups extend these rather than start fresh.

## Carried over (to schedule somewhere)

From the Milestone 8 reviews: floating costs ~20 µs a body (cheaper floating, ideas inbox); the crowd's gathering cost
(9A); no quality settings; volumetric fog (NoorSky phase C, "later 8 or 9"); 8D's open questions (the dry valley's
slower opaque pass; waves that turn and break; planar reflections). From the ideas inbox: Tom's Blender rigging add-on,
the in-engine animation studio, crisp stylised waves (crest glow).

## Questions only Tom (and Noor) can answer

1. **The groups and their order**: 9A big worlds → 9B land and water → 9C roads → 9D settlements → 9E vegetation and
   simulation → 9F GIS (optional) → the showcase. Right, or should something move?
2. **Which game should Milestone 9 serve first?** The Uncounted, Chapter Two (does it have villages and roads, or
   zones?), Thalassa (would it rather have ocean floors and caves, which are more Milestone 10's voxels), Thornwake's
   kind of world, or the NoorWorldShowcase on its own?
3. **Thornwake's world generator**: port its ideas into NoorEngine's procedural graph (my suggestion), or keep it as a
   separate generator that writes files (like NoorArchitect)? And may the engine's docs describe and cite its
   decisions (it's your private repository)?
4. **NoorArchitect and NoorRoads**: are they ready to be called headlessly with a recipe and seed (a command line or a
   DLL with a C entry point)? Which recipes should a village use first? Should NoorRoads exist as a separate generator
   at all, or is the road graph simply the engine's (my suggestion: the engine's, with NoorRoads making road
   *meshes and junctions* from it later)?
5. **"Simulation"**: what did you and Noor mean? Ecology over time, traffic, villagers going about their day, weather
   and erosion? I'd keep 9 to an interface and one example.
6. **GIS in Milestone 9, or later?**

When these are answered, this becomes `M9_PLAN.md` with groups, parts, proofs and risks, like
[M8_PLAN.md](M8_PLAN.md).
