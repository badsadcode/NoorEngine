# The procedural graph

Milestone 7 ([plan](../milestones/M7_PLAN.md), [ADR-014](../decisions/ADR-014-procedural-graph.md)).
**Works:** the graph file, typed nodes in a registry, validation, deterministic evaluation with keys
(part 1); the results cache, worker threads with progress and Cancel, `noor_cli generate` and the
editor API's background runs (part 2). **Planned:** terrain as a client (part 3), the graph editor
(part 4), scatter (part 7).

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
files). Tests: `tests/unit/test_procgraph.cpp` (11 cases), `cli.generate`, `api: …a graph runs in
the background…` and `api.generate_matches_cli`; test data `tests/data/procgraph_project`.
