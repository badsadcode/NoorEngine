# Milestone 9 plan: deep procedural world building

**Goal (spec):** deep modular procedural world building: a road graph, road and terrain modifiers, building
footprints and rooms, parcels and settlements, vegetation ecology and simulation interfaces; streamed worlds.

**Gate (spec):** one change to an input (moving a road) reliably updates everything that depends on it and protects
manual overrides; the whole workflow can be automated headlessly; a streamed world demo loads without corrupt
references.

**How it's split** (agreed 2026-11-20 by Tom and Noor, from the [proposal](M9_PROPOSAL.md)): groups in this order,
each with its own plan, ADR, parts and review, as in Milestone 8.

## Principle 0: NoorEngine stays a general-purpose engine

This is the milestone's first requirement, set by Tom and Noor. Every decision below is checked against it.

NoorEngine is a general-purpose game engine, not a procedural survival-game engine. It is meant for retro and modern
shooters, 2D and 2.5D platformers, third-person adventures, racing and driving, space, submarines and underwater
games, strategy and management, small arcade and puzzle games, large open-world survival games, and worlds that are
handcrafted, procedural, or both. Milestone 9 **adds capabilities without imposing** a genre, a world structure, a
visual style, a scale or a way of working.

1. **Everything in Milestone 9 is optional.** A project uses streaming, terrain generation, roads, settlements,
   vegetation or simulation only if it asks for them.
2. **Hand-made scenes keep working** without any of them. A `.noorscene` stays a complete game level.
3. **Small games pay nothing** for big-world systems they don't use: no extra threads, memory or frame time. This
   is measured (the yardsticks, 9A part 1), not assumed.
4. **Generation complements level design, it doesn't replace it.** Generated and hand-placed things live in the
   same scenes, edited with the same tools.
5. **Each system is useful on its own**, outside the NoorWorldShowcase. A space game may want big-world coordinates
   and nothing else. A racing game may want roads without buildings. A retro shooter may want none of it.
6. **The editor stays as comfortable for a small hand-made scene** as for a big generated world.
7. **No assumptions** about camera, genre, art style or character type.
8. **Existing projects and workflows keep working**: old files open, old goldens pass, migrations where unavoidable.

**The NoorWorldShowcase demonstrates what the engine can do. It doesn't define what a NoorEngine game is.** If
someone wants to make Pong in NoorEngine, they should be able to (and 9A part 1 makes sure of it).

## The groups

| Group | What | Status |
|---|---|---|
| **9A: Big worlds** | Large-world coordinates, world cells loaded and unloaded in the background on a budget, a cached scene gather, references that stay valid across cells, saving and restoring streamed state, the streaming inspector; **all optional**: a plain scene needs no world grid | **Next** ([parts below](#9a-big-worlds)) |
| **9B: Land and water** | Landforms, world-type recipes, climate and moisture, drainage and watersheds, lakes at their spill level and river networks emitted as 8D water bodies, biome maps, all deterministic from recipe and seed, as nodes in the procedural graph (ADR-014) over the existing terrain and water. No second terrain or water system | Planned |
| **9C: Roads** | An engine-owned road graph (splines, junctions, intersections) with editor tools, terrain cut and fill in a protected layer, bridges and crossings, roadside exclusion masks, manual edits that survive regeneration; the same spline tool edits rivers. Usable in hand-made scenes as well as generated ones. NoorRoads stays an independent generator for road meshes, junction geometry, profiles, structures and roadside assets | Planned |
| **9D: Settlements and buildings** | Blocks and parcels, footprints, settlement layouts, placement, calling NoorBuilding headlessly, importing what it returns (Noor Interchange), dependency tracking, manual changes kept. First test: a small mixed village (cottages, detached houses, a few shops, regional variations) | Planned; starts with an **audit** of NoorBuilding and NoorRoads |
| **9E: Vegetation ecology and simulation** | Suitability (biome, moisture, slope, elevation, light), species, forest layers and populations, urban constraints from 9C/9D's masks, protected manual vegetation; the **simulation interface** with one example: a forest growing, ageing, spreading and recovering over simulated time, deterministic | Planned |
| **The showcase** | NoorWorldShowcase: a generated, explorable, streamed world: terrain and biomes, forests, a lake and river, roads and bridges, a small village of generated buildings, sky and weather, the generation graphs open, manual edits surviving regeneration, Alden exploring it. The milestone's gate review | Planned |
| ~~9F: GIS~~ | **Deferred to a later milestone** (Tom and Noor). Milestone 9 must not block it: see [Ready for GIS](#ready-for-gis-later) | Later |

**The order changed after 9A part 1** (2026-11-20): Tom asked for 2D, 2.5D and isometric games too and left the order to
me. Next come 2D foundations, then gameplay foundations (spawning, UI, saving, navigation), then back to 9A part 2. Why:
[The order from here](#the-order-from-here-sage-2026-11-20).

The game Milestone 9 serves first is the **NoorWorldShowcase**, with a Thornwake-style procedural landscape as the
technical reference: a world that feels geographically coherent, where terrain, water, vegetation and settlements
shape each other. Nothing in the engine may become Thornwake-specific. The same systems must serve The Uncounted,
Thalassa, Baltic Drive and projects nobody has thought of yet.

## Rules for the whole milestone

### Thornwake is a reference, not a source

Its ideas, algorithms and lessons (landforms, world recipes, hydrology, terrain shaping, rivers, lakes, biomes,
vegetation populations, reproducible generation, fingerprint tests) are **re-implemented natively** in NoorEngine's
procedural graph where they fit. There is no runtime dependency on Thornwake, and its generator architecture isn't
copied wholesale without checking that it suits us. Where a subsystem is better as an independent generator, that
option stays open.

**Thornwake isn't treated as perfect** (Tom, 2026-11-20). Its algorithms may need tweaks. Where we can do better
(faster, more realistic, simpler, more deterministic), we do, and we say what changed and why. Each ported idea is
measured against what Thornwake's version produced, so "better" is shown, not claimed.

**Public docs** may discuss its design principles and lessons. They don't publish its source code or private
implementation details without Tom's explicit approval.

### Generated content remembers where it came from; manual edits survive

This is one of the milestone's most important requirements. A generated element records what made it: the
generator, its inputs, and a **stable key** that is the same every time the same input produces it. Manual changes
are kept as **overrides**, separate from the generated result: a moved building, a deleted tree, an added object.
When an input changes (a road moves), only what depends on it regenerates. Unrelated manual changes stay. An
override whose target no longer exists, or that now conflicts with the generated result, is **reported as a
conflict** and kept for the user to resolve, never silently dropped.

Dependencies are whatever the project's graph says they are. There's no built-in terrain → road → settlement chain,
because projects build different pipelines. Regeneration is incremental wherever practical: by graph node, and by
region (ADR-014's tiles already are).

This builds on ADR-014 (cached evaluation, override layers) and will be written down as **ADR-039, generated content
and overrides**, at the start of 9B, before any generator writes entities into a scene. 9A only has to keep room for
it: cell files keep unknown components verbatim (ADR-004), so a `provenance` block survives.

### Everything works headlessly

Every generation step, world operation and import runs from `noor_cli` without NoorEditor, with documented commands
and machine-readable (JSON) diagnostics. This is for CI, batch work, server-side world preparation, external
generators and AI-assisted workflows. **The editor and the CLI call the same engine functions.** No generation logic
lives only in editor UI code (ADR-025 already draws that line).

### Independent generators: a file contract

NoorEngine owns the canonical editable data: the road graph, parcels and footprints. Independent generators
(the NoorArchitect family: NoorBuilding, NoorRoads, later NoorVegetation and NoorBridges; also NoorLevel and NoorFengShui) are processes with a documented, versioned file
contract: Noor Interchange, or an explicitly versioned extension of it. A generator must:

- accept a recipe, a seed and geometric constraints;
- run headlessly;
- return assets and structured manifests;
- be deterministic where practical;
- report errors and diagnostics clearly;
- depend on nothing inside NoorEngine.

This decides **ADR-007** (plugin ABI): a generator is a process first; in-process plugins later, if ever. Nobody
assumes NoorBuilding or NoorRoads already meet this. **9D starts with an audit** of both. Any adaptations needed
are written up separately, for their own repositories.

### Simulation: an interface plus one example

Simulation means a world that changes over simulated time rather than being a fixed snapshot. A simulation module:

- reads the world data it needs;
- advances by a given time interval;
- returns **structured changes**;
- saves and restores its own state;
- runs without the editor;
- leaves unrelated systems alone.

It is deterministic for the same initial state, seed and time steps. The one example in Milestone 9 is **vegetation
ecology** (9E). Traffic AI, NPC societies and economies are **not** part of Milestone 9.

### Ready for GIS (later)

GIS import is deferred, but nothing in Milestone 9 may block it. ADR-013 leaves room for an optional geographic
reference on a world (a datum and projection, an origin), and its positions are precise enough for real-world
extents. Generated content records its provenance, which is where dataset attribution and licences will go. GIS
will fill the **same** world systems as procedural generation: elevation into terrain, OpenStreetMap roads and
railways into 9C's graph, footprints into 9D, rasters into the procedural graph's fields, with "regenerate after
re-import" working like any other changed input. There won't be a separate GIS kind of world.

### Tests and quality gates

Besides each part's own proofs, Milestone 9 has to show:

- large-world precision and streaming;
- safe cell loading and unloading, and stable references across cells;
- deterministic generation;
- dependency-aware regeneration that preserves manual changes;
- generator failures and recovery;
- headless generation;
- Noor Interchange compatibility;
- performance and memory;
- backward compatibility with existing scenes.

And it keeps a **regression set of different kinds of projects**, run after every part from 9A part 1 onward, so
Milestone 9 can't quietly make NoorEngine worse for small games:

| Yardstick | Project | What it guards |
|---|---|---|
| A small hand-made scene, no generation | a single cube in an otherwise empty scene (new, `tests/data`) | start-up, memory, threads, frame time of the smallest possible game |
| A simple 2D game | **NoorPong** (new sample, part 1): an orthographic camera, sprites, Lua | 2D works; nothing 3D-world-shaped is required |
| A compact 3D level | NoorFPS-lite | hand-made levels, physics, scripts |
| A procedural landscape | Hillside (graph terrain, scatter, water) | Milestones 7–8 unchanged |
| A large streamed environment | 9A's streamed world (from part 5) | streaming itself |

---

# 9A: Big worlds

## Where 9A starts

- **One scene is the whole game.** A `.noorscene` holds every entity (ADR-004). `make_world` loads all of it, and
  the scene's models, textures and terrains are loaded the first time they're seen. That's fine for every game
  we've made so far, and it has to stay that way for them.
- **Single precision everywhere.** Transforms are float32 (ADR-004, ADR-022). A float has about 2 mm of resolution
  at 16 km and 4 mm at 32 km, so a camera far from the origin shakes and shadows swim. The spec (9.5) wants a
  32 × 32 km or 64 × 64 km world to be comfortable. ADR-013 is still open.
- **The gather walks everything, every frame.** `game::collect_view` rebuilds the whole `SceneView` each frame from
  every entity. 8C measured it: 250 Aldens cost about 9 ms of CPU, much of it gathering and posing. It has no
  spatial structure, so whatever is off-screen or far away still costs CPU.
- **No background loading.** Models and textures load, and are uploaded to the GPU, on the main thread when first
  needed. In a world that loads as you walk, that's a stall.
- **What helps:** stable entity GUIDs, with unknown components kept verbatim (ADR-004); the procedural graph's
  tiles, cache and cooked outputs (ADR-014); terrains in tiles with LOD (ADR-031); instancing and LOD (ADR-032);
  `noor_cli` (build, cook, generate); built-game identity checks; Baltic Drive's moving origin and Thornwake's
  worker-thread terrain streaming as references (both Godot, so their ideas, not their code).
- **No orthographic camera.** Cameras are perspective only, so a true 2D game (the Pong yardstick) can't be made
  yet. Part 1 adds one: it's small and it's what principle 0 asks for.

## Decisions (to propose with the parts named)

- **ADR-013, world coordinates** (part 2). Proposed:
  - **World positions in double precision** where positions live: the scene's and runtime's transforms of
    root-level entities (children stay float, relative to their parent), scripts' position APIs (Lua numbers are
    already doubles; C# gets double vectors beside the float ones), and saved files (JSON numbers are already
    written with enough digits).
  - **Camera-relative rendering**: every draw's world matrix has the camera's position subtracted in double, then
    goes to the GPU as float, so precision is always best right where you look. It's the same cost for a small
    scene and changes no picture beyond float rounding.
  - **Physics with a moving origin**: Jolt stays single precision and works around a physics origin that moves when
    the active area drifts more than 1 km from it (Baltic Drive's approach). Part 2 measures this against Jolt's
    `JPH_DOUBLE_PRECISION` build before choosing.
  - A small game never moves the origin, so nothing changes for it.
  - Optional geographic reference on a world for GIS later.
- **ADR-038, worlds and cells** (part 4). Proposed:
  - **A world is an optional asset** (`.noorworld`): a persistent scene, always loaded (the player, global
    managers, the sky), plus a grid of **cells**. Each cell is a file of entities in the scene format, with a
    project-chosen cell size (256 m by default).
  - **A plain `.noorscene` is still a complete game.** Nothing about cells is required.
  - **Entity GUIDs are global across a world.** A saved **world index** maps GUID to cell, so any reference can be
    checked at save time. "No corrupt references" is checked, not hoped for.
  - **A reference into an unloaded cell is a handle that says "not loaded"**: never a dangling pointer, never a
    crash.
  - An entity belongs to the cell its root position is in. When a moving entity crosses into another cell, it
    changes owner, and what it changes to is recorded.
  - Big terrains are split into cell-sized pieces whose edges match exactly (the spec's "chunk IDs and boundary
    seams").
- **Loading is asynchronous and budgeted**: reading, parsing, decoding and building on worker threads; activating
  entities and uploading to the GPU on the main thread under a per-frame time budget; loading rings around one or
  more **streaming sources** (the player, the camera, a script's point of interest) with hysteresis so cells don't
  flap.
- **Gameplay never depends on when a cell finished loading.** Tests and built-game checks use a deterministic
  mode where cells load at fixed frames. The real asynchronous mode is measured separately for stalls and memory.

## Parts (each ends tested, documented and pushed)

| Part | What | Proves |
|---|---|---|
| 1 ✅ | **The yardsticks**: the regression set above (the cube, **NoorPong** with an **orthographic camera** on the `camera` component, NoorFPS-lite, Hillside). `noor_player --report` gains memory (process and GPU), thread count, load time and frame-time percentiles. Baseline numbers are recorded in `docs/milestones/M9_YARDSTICKS.md`, plus a check that fails when a small project gets slower or bigger beyond a stated tolerance | NoorPong plays in the player and as a built game identically (a scripted rally, the ball's path to the bit); an orthographic golden; the baseline table, measured; the check catches a deliberately added 1 ms stall |
| 2 | **Large-world coordinates** (ADR-013): double positions in the scene and runtime, camera-relative rendering, the physics moving origin (measured against Jolt's double build first), script APIs, the editor's gizmos and grid far from the origin | A scene repeated at the origin and at (30 km, 0, 30 km) renders the same picture, with no shake (consecutive frames of a still camera identical), and objects resting and colliding identically; precision checked out to 64 km; **every existing golden still passes**; a built game identical; the yardsticks unchanged |
| 3 | **The cached gather**: the `SceneView` **and the draw list built from it** kept between frames and updated only from what changed (moved, added, removed, edited); a spatial grid for CPU culling and range queries; the same pictures. (Part 1 measured where the time goes: Hillside's gather is 0.17 ms, its draw list 5.1 ms, mostly the scattered forest) | Every golden identical; 8C's 250-Alden crowd's gather cost measured before and after (target: under a third); a static Hillside frame gathers in microseconds; the yardsticks equal or faster |
| 4 | **Worlds and cells** (ADR-038): `.noorworld`, cell files, the persistent scene, the world index, cross-cell handles, ownership changes; `noor_cli world split` (scene → world), `merge` (world → scene), `validate` (every reference, every asset, JSON diagnostics) | A scene split into cells and merged back is identical to the original; `validate` finds every planted broken reference (a deleted target, a missing cell, a duplicated GUID) and nothing in a good world; a world loaded all at once plays like the scene it came from (built-game check) |
| 5 | **Streaming**: background loading and unloading of cells (files, models, textures, terrain pieces) with a per-frame budget for activation and uploads; streaming sources, rings and hysteresis; a first streamed test world (terrain from a graph, cooked per cell, plus scattered models), generated headlessly | A camera flown across the world for minutes: no frame over budget from loading (worst frame and percentiles measured), memory back to its level after 100 load–unload cycles (no leaks), terrain seams closed (a golden across a cell edge), no corrupt references; deterministic mode identical in a built game |
| 6 | **Saving and restoring**: a cell's runtime changes kept when it unloads and restored when it loads (a moved crate stays moved); saving and loading a game in a streamed world; lifecycle hooks for scripts (`on_load`, `on_unload`, spec 9.4); physics bodies crossing cell edges | A crate pushed, its cell unloaded and reloaded: it's where it was, to the bit; a save made mid-stream and loaded in a fresh process gives the same world (fingerprints); a body sliding across a cell edge keeps moving without a hitch; scripts get each hook exactly once |
| 7 | **Editing worlds and the streaming inspector**: the editor opens a world, loads cells around its camera, saves edits to the right cells, undoes across them; a **World panel** (the grid with loaded, loading and unloaded cells, memory and entities per cell, budgets, the streaming sources); a cell overlay in viewports and the player (`--streaming-debug`). Opening a plain scene looks and feels exactly as before | API tests (edit an entity in a far cell, save, reload: it changed and nothing else did; undo across a cell edge); the smoke test opens a world and a plain scene; the World panel's numbers match the player's report |
| 8 | **Sample + review**: the streamed demo world (16 × 16 km to explore; a walk or ride across it), its numbers beside the yardsticks, the 9A review | The 9A gate: large worlds stream reliably, small scenes stay simple to create and run |

**9A's acceptance (Tom and Noor):** large worlds stream reliably, while small scenes stay simple to create and run.
A simple hand-made level works without configuring any world grid.

## Part 1 result (2026-11-20)
Done. **The orthographic camera**: `"projection": "orthographic"` on the `camera` component with a `height` (metres
shown top to bottom) and a `far` plane; `render::Camera::projection()` chooses between `math::perspective_reverse_z`
and the new `math::orthographic_reverse_z` everywhere a projection is built (the frame, shadow cascades, which become
boxes, culling, levels of detail, the player's readbacks). Shaders get a view direction that is the same for every
pixel (`view_ray` in `frame.hlsli`: lighting, particles, the sky, volumetric clouds); sprites face the view's direction;
the editor picks and sizes its gizmos with parallel rays; **Create ▸ Camera (2D)**. **NoorPong** (`samples/NoorPong`,
`tools/samples/make_noorpong.py`): bat and ball in Lua on that camera, a pixel-art sprite ball, the computer playing a
side until someone presses its keys, deterministic at the fixed step. **The yardsticks**: `noor_player --report` gains
`process` (working set, private memory, GPU-resident memory, threads, seconds to the first frame), CPU per frame and p99
timings, and `--stall-ms`; four yardsticks with a measured baseline and limits (`tests/yardsticks.json`,
[M9_YARDSTICKS.md](M9_YARDSTICKS.md)).

Proved: orthographic projection and parallel picking by hand (unit and API tests); a NoorPong golden; the built NoorPong
plays the same twenty seconds to the bit, with points scored (`player.noorpong`); every yardstick within its limits, and
the cube with a deliberate 1 ms stall caught (`yardstick.catches_a_stall`); every existing golden unchanged.

**What the baseline found** (written down, not fixed here): every game carries about 117 MB of GPU render targets,
shadow maps included even with shadows off; an empty see-through pass costs about 0.3 ms; and Hillside's 6 ms of CPU a
frame is the draw list (5.1 ms, the scattered forest), not the gather (0.17 ms), so part 3's cache now covers the draw
list too.

## The order from here (Sage, 2026-11-20)

Tom listed the games he wants NoorEngine for: 2D platformers; first-person action and RPGs; third-person action and
adventure; RPGs in general; dungeon crawlers; open-world survival; driving; isometric action RPGs (Diablo- and
Torchlight-like); "pretty much every game type". He asked me to choose the order and explain it. The rule I used is the
spec's own first question for choosing a task (29.2): **does it unlock several other things?** So: what do the most games
on that list need that the engine doesn't have yet?

| Missing today | Games on the list that need it |
|---|---|
| **Spawning things at runtime** (a bullet, an enemy, a pickup: prefabs from scripts), timers, scripts using other scripts (`require`), moving between levels | Every one of them (M4's review left these as exceptions "until the first game needs them") |
| **Navigation and AI**: finding a path round walls, agents that follow it | FPS enemies, RPGs, dungeon crawlers, third-person action, isometric action RPGs, survival (animals), driving (traffic) |
| **Game UI**: menus, buttons, inventory grids, dialogue boxes (today: text and rectangles drawn each frame) | Every game's main menu; RPGs, crawlers, survival and isometric RPGs above all |
| **Saving and loading a game** | RPGs, adventures, crawlers, survival, open worlds |
| **2D**: plane physics, sprite layers, tilemaps, a 2D editor view | 2D platformers, Diablo-1-style isometric games, The Uncounted |
| **Big worlds**: coordinates, streaming (the rest of 9A) | Open-world survival, driving, big RPGs |
| **Vehicles** (spec 19.3, Milestone 10) | Driving games |
| **Generated worlds**: land and water, roads, settlements, ecology (9B–9E) | Open-world survival, driving, big RPGs |
| **Generators as tools**: NoorLevel's blockouts, NoorBuilding's buildings, NoorFengShui's furnished rooms | Dungeon crawlers, FPS levels, interiors for any 3D game |

So the order:

1. **2D, 2.5D and isometric foundations** (next, below). Tom asked for it; it's small and self-contained; it serves the
   first real NoorEngine game (The Uncounted, Chapter Two: sprites in zones). Note that **3D isometric games like
   Torchlight already work** with part 1's camera tilted (3D models, a fixed angled view); this group is for the 2D kind
   (Diablo 1 and 2: sprites, tiles, sorting) and for platformers.
2. **Gameplay foundations** (proposed, a plan of its own): spawning and timers, game UI, saving and loading, level
   changes, navigation and AI agents. **This is a change to the roadmap**: the spec has no milestone for these, and
   Milestone 9 wouldn't add them. But they're what turns a scene into a game in nearly every genre Tom listed, and a big
   generated world with nothing that can walk round it, be saved, or show a menu isn't a game either.
3. **Back to 9A, big worlds** (parts 2–8), then **9B–9E** and the showcase, as planned. Vehicles fit beside 9C (roads
   and cars belong together; the showcase could add a drive); the generator contract (ADR-007, the 9D audit) brings in
   NoorLevel and NoorFengShui as well as NoorBuilding.

Everything stays measured by the yardsticks, so going 2D or adding gameplay systems can't quietly slow 3D down, or the
other way round.

### 2D, 2.5D and isometric foundations (next)

What I'd build, and why each piece (it becomes a plan with proofs when it starts):

| # | What | Why |
|---|---|---|
| 1 | **2D physics on Jolt**: bodies locked to a plane (Jolt's allowed degrees of freedom: move in X and Y, turn about Z), 2D shapes (box, circle, capsule, polygon) as thin 3D ones, a 2D character controller (platformer: ground, slopes, one-way platforms, coyote time as script options) | A platformer needs physics that never drifts out of its plane. Locking Jolt keeps **one** physics engine (determinism, built-game checks, tooling) instead of adding Box2D beside it |
| 2 | **Sprite sorting and see-through sprites**: sorting layers and order in layer, sorting by Y for top-down and isometric views, soft-edged (alpha) sprites, unlit sprites (their own colours, no sun needed) | Today sprites are lit cutouts sorted by depth, right for 2.5D in a 3D world, wrong for most 2D art: a 2D game decides what's in front by layer, not distance |
| 3 | **Tilemaps**: a tileset asset (from a sprite sheet), a `tilemap` component in square, isometric and hexagonal layouts, tiles drawn in batches, colliders made from solid tiles, auto-tiling rules later | The backbone of platformers, top-down RPGs (The Uncounted's zones?) and isometric games; drawing thousands of tiles one sprite at a time would be slow |
| 4 | **The editor in 2D**: a 2D view (XY, orthographic, pan and zoom, no orbit), grid snapping in pixels or tiles, tile painting, sprite placement by drag and drop | Principle 0, item 6: a small 2D scene must be as comfortable to make as a 3D one |
| 5 | **Pixel-perfect cameras and parallax**: snapping to whole pixels at a chosen pixels-per-unit, integer scaling for retro resolutions, camera follow with dead zones and bounds, parallax layers | Pixel art shimmers when a camera moves by fractions of a pixel; every 2D game needs a following camera |
| 6 | **A sample and a review**: a small platformer (our own art) and an isometric scene, both yardsticks | The proof, and two more yardsticks guarding 2D from then on |

**2.5D** comes mostly from these plus what exists: a 3D world played on a plane (part 1's locked physics with 3D
models), 8-direction sprites in a 3D world (8B, The Uncounted's style), or an isometric camera over 3D models. **2D
lighting** (normal-mapped sprites, 2D lights and shadows) and **skeletal 2D animation** are worth their own later step.

## Highest risks (settle early)

1. **Double precision touches everything.** Transforms, scripts, physics, the editor, file formats. Part 2 does it
   behind the existing APIs where it can, with float APIs kept for local maths. It's measured on the yardsticks so
   small games don't pay for it.
2. **Physics precision.** A moving origin for Jolt means shifting every body at once without disturbing contacts or
   determinism. Jolt's double-precision build is the alternative, at a cost in speed and memory. Part 2 measures
   both before choosing.
3. **Loading without stalls on today's renderer.** Uploads are synchronous on the main thread. Streaming needs an
   upload queue with a budget, and models built off the main thread. This is the core of part 5, and the worst frame
   is measured, not just the average.
4. **Determinism under asynchronous loading.** Built-game identity checks have protected every milestone. With
   streaming, *when* a cell arrives varies, so gameplay must not depend on it. A deterministic loading mode for
   tests, and a rule that cells activate only at frame boundaries, keep the checks meaningful.
5. **The editor's one-scene document.** Undo, selection and saving assume one scene. Part 7 extends them to a world
   without changing how a plain scene feels. That's principle 0, items 6 and 8.
6. **Small-game cost.** Guarded from part 1 onward by the yardsticks: no new threads, memory or frame time for a
   project without a world.

## Open questions

1. Is a 256 m default cell size right? (My call unless a game says otherwise: Baltic Drive and Thornwake both
   streamed smaller terrain tiles than their object cells.)
2. 9A part 8's demo world: a walk with Alden, or a drive? A drive crosses more cells per minute, so it's the
   harsher test, but roads (and vehicles) come later. I'll start with Alden on foot plus a fast camera flight, unless
   Tom wants something else.

## Not in 9A (planned later)

- Terrain generation beyond what the test world needs (9B), roads (9C), settlements (9D), vegetation ecology (9E).
- Networking coordinates (ADR-017) and streamed navigation (spec 19.4): ADR-013 leaves room, nothing more.
- Planet-scale worlds: an expansion goal (spec 9.5), not promised.
- Texture streaming (mip levels loaded by distance): only if part 5's memory numbers say it's needed.
