# The procedural graph

Milestone 7 ([plan](../milestones/M7_PLAN.md), [ADR-014](../decisions/ADR-014-procedural-graph.md)).
**Works:** the graph file, typed nodes in a registry, validation, deterministic evaluation with keys
(part 1); the results cache, worker threads with progress and Cancel, `noor_cli generate` and the
editor API's background runs (part 2); tiles as regions, terrain nodes, terrains built on graphs
(part 3); the graph editor (part 4); scatter, instance sets and the scatter component with its
hand-made layer (part 7).

## A graph file (`.noorgraph`, format 1)

```json
{ "noor_graph": 1, "seed": 7,
  "nodes": [
    {"id": "hills", "type": "noise", "version": 1, "params": {"height": 30, "scale": 120}, "position": [0, 0]},
    {"id": "limit", "type": "clamp", "version": 1, "params": {"min": 0, "max": 25}, "position": [200, 0]}
  ],
  "links":   [{"from": "hills.height", "to": "limit.in"}],
  "outputs": [{"name": "terrain", "from": "limit.out"}] }
```

- **Node ids** are names chosen when a node is added (`hills`, not `node_17`). They identify the node
  in links and errors, **and they seed its randomness**, so they must stay put. No `.` in ids.
- **`seed`**: the graph's seed, an unsigned 64-bit integer (never a float).
- **Fields this engine doesn't know** (on nodes, e.g. an editor's comment) are kept as they are when
  saving. Saving is deterministic: the same graph always writes the same bytes.
- `.noorgraph` files are assets (importer `procgraph`).

## Types and units

| Pin type | What |
|---|---|
| `scalar` | a number |
| `vector` | three numbers |
| `seed` | an unsigned 64-bit seed |
| `field` | a grid of samples centred on the origin (like a terrain: tiles of 256 cells, `cell` metres apart) |
| `instance set` | placed copies of assets (part 7) |
| `asset` | an asset reference |

**Units:** none, metres, ratio (0..1), degrees. A **height field** is a field in metres; a **mask** is
a field in ratio. Linking different units is an error, never a silent conversion. Some pins take
any unit; some outputs take their unit from an input (`clamp`'s output is in whatever went in), and
some choose it with a parameter (`constant`, `remap`). Those are checked while the graph runs,
since only then is the unit known. A plain number (unit none) combines with anything.

## The nodes (part 1)

| Node | Inputs → outputs | Parameters |
|---|---|---|
| `constant` | → `value` | `value`, `unit` |
| `math` | `a`, `b` (same unit) → `out` | `op`: add, subtract, multiply, divide (by zero gives 0), min, max |
| `noise` | → `height` (metres) | `tiles_x`, `tiles_z`, `cell_size`, `height`, `scale`, `octaves`: the terrain's own noise; the same seed gives the same heights as a terrain, bit for bit |
| `combine` | `a`, `b` (same size and unit), `t` (mask, optional) → `out` | `op`: add, subtract, multiply, divide, min, max, lerp (a to b by t) |
| `clamp` | `in` → `out` | `min`, `max` |
| `remap` | `in` → `out` | `from_min`, `from_max`, `to_min`, `to_max` (straight line, clamped), `unit`: same, metres, ratio (heights into a mask), none |

Every node has a **version**. A node's results may only change with a new version. A graph saved
with another version of a node is reported (newer: "made with a newer version"; older: no
migration yet) and that node doesn't run.

## The terrain nodes (part 3)

| Node | Inputs → outputs | Parameters |
|---|---|---|
| `stamp` | `in` (metres) → `out` | `x`, `z` (centre, in the terrain's space), `radius`, `height` (negative: a hollow), `shape`: hill (round top), plateau (flat top) |
| `terrace` | `in` → `out` | `step` (m), `sharpness` (1: flat levels and cliffs, 0: no steps) |
| `height_band` | `in` (metres) → `out` (mask) | `min`, `max`, `soft` (fade width at each edge, m) |
| `slope` | `in` (metres) → `out` (mask) | `min_degrees`, `max_degrees`: 0 flatter than min, 1 steeper than max (its angle thresholds use a cosine made of + − × only) |
| `heightmap` | → `height` (metres) | `image` (a picture asset's id), `min_height`, `max_height`, `tiles_x`, `tiles_z`, `cell_size`: the terrain's own heightmap reading |
| `paint_mask` | → `mask` (ratio) | `paint` (a terrain's `.noorpaint` asset id), `layer` (0: the ground underneath, 1–3 painted), `tiles_x`, `tiles_z`, `cell_size`: how much of that material each sample has (part 8: plant on the painted meadow, not on the path) |

## Scatter: instance sets (part 7)

| Node | Pins | Parameters |
|---|---|---|
| `scatter` | `height` (metres, optional), `mask` (ratio, optional) → `instances` | `asset` (the model's id), `density` (copies per 100 m² where the mask is 1), `spacing` (no two closer, m), `scale_min`, `scale_max`, `width`, `depth` (without a height field), `max_count` |
| `exclude` | → `mask` (ratio) | `paths` ("x,z x,z …; …"), `width`, `circles` ("x,z,r; …"), `soft`, `tiles_x`, `tiles_z`, `cell_size`: 0 on paths and in circles, 1 elsewhere |
| `merge` | `a`, `b` (instance sets) → `instances` | `spacing`: `b`'s copies closer than this to one of `a`'s are left out |

**How `scatter` places things.** The area is cut into cells of `spacing / √2`, and each cell gets
one candidate from **its own random stream** (the node's seed mixed with the cell's number): a
random point in the cell, a turn, a size, and a roll against `density × mask`. The candidates are
taken in cell order; one closer than `spacing` to a copy already kept is dropped. That gives
natural, even spacing (blue noise), and every copy depends only on its cell, so the result is the
same on every machine and every run. Turns use a sine and cosine made of + − × only. Each copy's
**id** is a hash of its cell under the seed: the same seed and settings give the same ids, which is
what hand-made edits refer to.

`density` is what you ask for; spacing limits what you get. Random placement can't pack copies
tighter than about 1.1 per 100 m² at 8 m spacing, so near that limit many candidates are dropped
(the unit test's forest keeps 210 of the 377 asked for at density 1, spacing 8). Well under it you get
close to what you asked (the test asks for about 75 at density 0.2 and accepts 55 to 90).

Masks are ordinary fields: a gentle-slopes mask (`slope`, then `remap` 1 → 0), a height band,
`exclude` for paths and clearings, multiplied together with `combine`. Several species: one
`scatter` each, joined by `merge` (the first wins where they meet).

### The instance set file (`<output>.instances.json`)

`noor_cli generate` and cooking write each instance set output as plain JSON
(`noor/asset/instance_set.h`), readable by any tool:

```json
{ "noor_instances": 1,
  "space": {"up": "+Y", "handedness": "right", "units": "meters"},
  "assets": ["719beb3e-327d-56ed-8d54-be8882f405fd"],
  "fields": ["asset", "id", "x", "y", "z", "qx", "qy", "qz", "qw", "scale"],
  "instances": [
    [0, "9f3c0a1b2c3d4e5f", 12.5, 3.25, -40.0, 0, 0.38, 0, 0.92, 1.07]
  ] }
```

One row per instance: an index into `assets`, the stable id (16 hex digits), the position in metres
(in the set's space: the scatter entity's), a unit quaternion, a uniform scale. Numbers read back
bit for bit. `tests/tools/instance_reader.cpp` reads it with nothing but a JSON parser (no engine
code); the test `tools.instance_reader` runs it on the test forest.

### In a scene: the `scatter` component

```json
"scatter": { "graph": <.noorgraph id>, "output": "trees", "ground": <terrain entity id> | null,
             "draw_distance": 45,     (optional: not drawn further from the camera; grass)
             "shadows": false,        (optional: cast no shadows; grass)
             "removed": ["9f3c0a1b2c3d4e5f"],
             "added": [{"id": "…", "asset": <model id>, "position": [x, y, z], "yaw": 30, "scale": 1}] }
```

- **Drawn** from the cooked output (`.noor/cooked/graphs/<graph id>/<output>.instances.json`, made on
  Save in the editor and by `noor_cli build` / `cook`), with levels of detail per instance and
  instancing (ADR-032). No entity per tree, so thousands cost little CPU.
- **Ground:** with a terrain named, every instance stands on it, sculpting included (as saved).
- **The hand-made layer:** in the Inspector, **Plant** and **Remove** brushes (left-drag on the
  ground; radius in the card; Plant puts a tree every radius along the drag, of the scatter's own
  model or one you pick). Removing a generated tree adds its id to `removed`; planting adds to
  `added`. Each stroke is one undo step. Regenerating (a new seed, new settings) never touches
  these lists: planted trees stay; a removed tree stays removed (with another seed it doesn't exist
  anyway, and if the seed comes back, it's still removed).
- **Collision** (part 8): each model's collision nodes become static shapes at every instance in games
  and Play: a tree's capsule trunk (where the trunk really stands; leaves don't collide), a rock's convex
  pieces. Models without collision nodes (grass) don't collide.
- **Many small things** (grass): placements are kept between frames and shared with the renderer, so a
  scatter of 20,000 costs about 3 ms a frame to collect in Release; `draw_distance` keeps far grass from
  being drawn (the edge is a hard line for now: no fade), `shadows: false` keeps it out of the shadow maps.
- **Not yet:** moving a single tree (remove it and plant one); trees following live (unsaved)
  sculpting under them; a fade at the draw distance.

## Tiles: regions that change alone (part 3)

Fields are made of tiles (256 cells, 257 samples, shared edges). Most field nodes are **tile-local**
(noise, combine, clamp, remap, stamp, terrace, height band): a tile of their output depends only on
the same tile of their inputs, so they're **keyed, cached and recomputed tile by tile**. A stamp
says which tiles it reaches; the others are its input's tiles **under the input's keys**, so nothing
after it reruns there either. Move a stamp inside one corner of a 2 × 2 terrain and exactly one tile
is recomputed in the stamp and in everything after it; the other three come out bit-identical with
unchanged keys (tested). `slope` reads neighbouring samples, so it works on the whole field: any
change upstream recomputes all of it. `heightmap` reads a file: its key includes the picture's size
and time.

## Terrains built on graphs (part 3)

```json
"terrain": { "base": "graph", "graph": "<.noorgraph id>", "graph_output": "terrain", "collision": true }
```

The graph's output (a height field in metres) is the terrain's generated base: sculpting and paint
sit on top as for any base, so a new seed or a changed node keeps the hand-made layers. The terrain
takes its tiles and cell size from the field. **Games never run graphs**: the field is cooked into
`<project>/.noor/cooked/graphs/<graph id>/terrain.noorfield` (with `graph.fingerprint.json`):
- by the editor when it shows a scene whose graph file changed, and on every Save (a picture a
  `heightmap` node reads may have changed);
- by `noor_cli build` (into the game) and `noor_cli cook`.

A cooked file whose bytes wouldn't change isn't rewritten, so terrains don't rebuild for nothing. A
graph that hasn't been cooked yet is reported plainly ("hasn't been generated yet: save the scene,
or run noor_cli cook"). Cooking runs on the editor's UI thread for now (fast with the cache; the
first run of a big terrain graph takes a moment).

## The graph editor (part 4)

Double-click a `.noorgraph` in the Content Browser (or **File ▸ New Procedural Graph…**) to open it in
a tab next to the viewport:

- **+ Node** adds any node type (the list and each node's help come from the engine).
- **Drag a node's title** to move it. **Drag from an output's dot to an input's dot** to link them; an
  input takes one link, so a new one replaces the old. **Click a wire** and press Delete to remove it;
  select a node and press Delete (or Delete node) to remove it with its links.
- **The side panel** edits the selected node's parameters (left at the default: greyed), names an
  output as a **graph output** (written to a file, usable as a terrain's base), and shows the last run:
  time or "from the cache", tiles computed / cached / passed, the value range and a 256 px preview.
  With no node selected it edits the graph's seed.
- **Runs after each change** (in the background, a quarter of a second after you stop editing; turn it
  off and use Run). Cancel stops a run. Thanks to the cache only what changed reruns.
- **Issues** (unconnected inputs, wrong units, cycles…) show on their node: a red border and ✕, the
  message in the tooltip and the side panel.
- **Ctrl+Z / Ctrl+Y** undo and redo inside the tab. Every change is saved at once, in the engine's own
  file format (the editor asks the engine to format the file), so a graph saved by the editor is byte
  for byte what `noor_cli` would write.
- Pin colours: fields cyan, numbers gold, the rest grey.

Like the material editor, the graph editor edits its file directly (with its own undo) rather than
through the scene's command history; the engine does all the checking, formatting and running
(editor API v21: `noor_graph_node_types`, `noor_graph_check`, the run's per-node results,
`noor_doc_generate_preview`). Not there yet: zoom, subgraphs, comments and groups, renaming nodes
(an id is also the node's seed), copy and paste.

## Determinism

- **Seeds:** a node's random streams come from `mix(graph seed ^ hash(node id))` and the stream's
  name, never from a shared generator. Adding, removing or reordering *other* nodes never changes a
  node's result (tested).
- **Maths:** integer hashing (SplitMix64), and only + − × ÷, floor and clamp in node code, as for the
  terrain. Uniform numbers come from the top 53 bits of the hash, with no library maths involved.
- **Order:** nodes run in dependency order, ties broken by node id, so the order is fixed.
- **Fingerprint:** a hash of the outputs' names and every bit of their values. A test pins one for a
  six-node graph; it's identical in Debug and Release (MSVC x64).

## Keys

Each node that runs gets a **key**: a hash of its type and version, its parameters (defaults filled
in, sorted, so parameter order and explicit defaults don't matter), its seed stream, and the keys of
the nodes feeding it. Change one parameter and exactly that node and everything downstream get new
keys (tested). Part 2 caches results under these keys.

## Cache (part 2)

Results are stored under their keys, in memory and in `<project>/.noor/cache/procgraph/<key>.npgc`.
A key covers everything a result depends on, so a cached result is always the right one. Change one
parameter and exactly that node and its dependants run again; the rest come from the cache (a test
counts them: 3 run, 3 cached). A damaged cache file is ignored and replaced; cache files are written
atomically and only for complete results, so a cancelled or crashed run never leaves half of one.
**There is no size limit yet:** delete `.noor/cache/procgraph` to clear it (it's derived data, like
the rest of `.noor`).

## Running: threads, progress, Cancel (part 2)

Nodes run on worker threads (one per core, at most one per node) as soon as their inputs are done.
Threads never change results: nodes are pure, the reported order is the fixed dependency order, and
issues are listed in that order too (a test compares one thread with eight: same bytes, same order).
A progress callback reports each finished node. **Cancel** skips every node not yet started; long
nodes check it as they go (`noise` checks after every row) and stop without keeping anything.

## Generating files, headless (part 2)

```text
noor_cli generate <project> <graph> [--out <folder>] [--threads <n>] [--no-cache]
```

Writes every declared output into the folder (default `<project>/Generated/<graph name>/`):

- **fields** as `<output>.noorfield`: `"NFIELD01"`, u32 version 1, u32 sx, u32 sz, f32 cell (metres),
  u32 unit (0 none, 1 metres, 2 ratio, 3 degrees), then sx × sz f32 samples, row by row (z rows of x),
  little-endian. Simple enough for any tool (a dozen lines of Python read it);
- **`<graph>.fingerprint.json`**, written last: the graph's hash, its seed, every node's type,
  version and key (sorted by id), each output's type, unit and hash (with the value for numbers and
  the file for fields), and the fingerprint. No timestamps, so the same graph writes the same bytes.

**Nothing is written unless every output was made:** a cancelled run, or one with an output that
couldn't be made, leaves the folder as it was and says why (issues per node). The editor runs graphs
through the same function (editor API v20: `noor_doc_generate_start`, `_status`, `_cancel`), and a
test checks that the editor's files and the CLI's are byte for byte the same.

## When things go wrong

Mistakes are reported **on the node they concern** (and the pin, when there is one), and the rest of
the graph still runs:

| Mistake | Reported on |
|---|---|
| unknown node type, wrong version, unknown or wrong parameter, unconnected required input | that node |
| a link of the wrong type or unit, a second link into one input, a link from an output that doesn't exist | the node receiving the link |
| a cycle | every node on it (`part of a cycle: a → b → a`) |
| a link to a node that doesn't exist, a bad graph output | the graph |
| a node failing while running (fields of different sizes, `min` above `max`) | that node; what depends on it is skipped ("input X has nothing: Y didn't run") |

## Where the code is

`engine/procgraph` (`noor_procgraph`, editor and tools only; the runtime-boundary test keeps it out
of games): `procgraph.h` (types, graph file, registry, validation, evaluation, cache, generate),
`procgraph.cpp` (checking and running), `nodes.cpp` (the built-in nodes), `run.cpp` (bytes, cache,
files, cooking). Tests: `tests/unit/test_procgraph.cpp` (14 cases), `cli.generate`, `api: …a graph runs
in the background…`, `api.generate_matches_cli`, `api: …a terrain on a graph base…`,
`cli.build_graph_terrain` + `player.graph_terrain`; test data `tests/data/procgraph_project`.
