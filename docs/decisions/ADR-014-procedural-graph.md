# ADR-014: Procedural graph: typed node assets, pure cached evaluation, override layers

**Status:** Proposed (Milestone 7; part 1 done, part 2 settles the cache and jobs) · **Date:** 2026-11-18 · **Milestone:** 7

## Context
Spec 15.1 asks for one shared procedural graph system: typed pins with units, deterministic seeds
with explicit stream ownership, cached intermediates with dependency invalidation, partial
recompute, background jobs with progress and cancellation, execution traces, reproducibility
fingerprints, and manual overrides as layers instead of destructive edits. Spec 15.5: every
generator is also callable headless. The Milestone 7 gate tests exactly that: reproducible outputs,
overrides that survive regeneration, region-limited invalidation, outputs usable outside.

What we have: terrain generation that is already deterministic (integer hashing, + − × ÷ only,
pinned fingerprint) with sculpt and paint layers that survive regeneration (ADR-031), a document
with undoable commands (ADR-025), assets with stable ids (ADR-006), Noor Interchange for files
other tools read (ADR-023). Tom's generators (NoorForm trees and rocks in NoorMD, BadShip,
BadBuilding) are separate .NET programs connected by files.

## Options
- **A: Graph as data, nodes as registered native functions, pure evaluation with a content-addressed
  cache.** A `.noorgraph` asset lists nodes, parameters and links; each node type is a C++ function
  with a declared version, typed pins and a parameter schema. Evaluation is pure: a node's output
  depends only on its type + version, parameters, its seed stream and its inputs, so its cache key is
  a hash of those. Overrides are separate layers that nodes read but never write.
  *Pro:* reproducibility and invalidation fall out of the keys; headless is free (no editor in the
  loop); easy to test. *Con:* nodes with side effects (writing assets, calling external programs) need
  a careful "output stage" outside the pure part.
- **B: Scripted generators** (C# or Lua scripts producing results, a graph only for wiring).
  *Pro:* flexible, quick to write. *Con:* determinism and caching depend on every script's discipline;
  hard to hash "what changed"; scripts are runtime code with a sandbox story we don't want here.
- **C: Embed an existing node framework** (Houdini-like engines, Blender geometry nodes, PCG-style
  frameworks). *Pro:* mature. *Con:* licences, a foreign data model, and none of them speaks our types
  or our determinism rules.

## Decision (proposed)
**A.**
- **Assets:** `.noorgraph` (JSON, versioned, deterministic order): nodes `{id, type, version,
  params, position}`, links `{from: node.pin, to: node.pin}`, a graph seed, declared outputs.
- **Types:** scalar, vector, seed, height field (tiles of 257², metres), mask (0..1 per sample),
  instance set (asset id + transform + per-instance seed), asset reference. Units on pins; a link
  between incompatible units is an error, not a silent conversion.
- **Seeds:** a node's random stream is `hash(graph seed, node id, stream name)`; no shared RNG, so
  adding or reordering nodes never changes other nodes' results.
- **Evaluation:** topological; independent nodes run on the job system; results are joined in a
  fixed order. Floating-point reductions use a fixed order (no `std::reduce` without one).
- **Cache:** results under `.noor/cache/procgraph/<key>` with `key = hash(node type, node version,
  canonical params, seed stream, input keys)`; a version bump invalidates. Regions: nodes that work on
  tiled data (height fields, masks, instance sets) are keyed **per tile**, so a change with bounds
  dirties only the tiles it touches.
- **Overrides:** hand-made layers (sculpt, paint, instance add/remove) are inputs a graph reads and
  never writes; regeneration recomputes the generated part only.
- **Errors:** cycles, missing inputs, type/unit mismatches and unknown node versions are reported on
  the node; a graph with errors still loads, saves and shows what it can.
- **Headless:** `noor_cli generate <project> <graph>` runs the same code and writes the same bytes as
  the editor, plus a fingerprint file (graph hash, node versions, seed, input asset hashes, output hashes).
- **Runtime boundary:** `noor_procgraph` is an editor/tools module; games load outputs (cooked like
  CSG). The boundary test gains it.
- **Plugins (ADR-007, open):** the node registry is designed for external registration (C ABI,
  versioned), but Milestone 7 ships built-in nodes only. External generators (NoorForm, BadShip…)
  enter through files until then.

## What part 1 settled (2026-11-18)
- **Units:** one `field` type with units (metres = height field, ratio = mask) rather than separate
  height-field and mask types. Pins may take any unit or take their unit from an input; units chosen
  by a parameter (`constant`, `remap`) are checked while running. A unitless number goes with anything.
- **Blame:** a broken link is the receiving node's problem (or the graph's), never the source's, so a
  healthy node never stops because someone linked to it badly.
- **Keys** hash parameters with defaults filled in and keys sorted: the same parameters always give
  the same key.
- **Evaluation** is single-threaded in part 1 (jobs come in part 2); the order is fixed by dependencies
  then node id.
- **Fingerprint** of the test graph identical in Debug and Release (MSVC x64), pinned in CI.

## Consequences
- Easier: reproducibility tests are hash comparisons; invalidation is "which keys changed"; the CLI
  and the editor can't disagree; terrain's existing generator becomes a set of nodes.
- Harder: every node must be pure and versioned (a behaviour change without a version bump would
  serve stale cache); large intermediate results need a cache size policy; side-effecting outputs
  (writing assets) are a separate, explicit stage.
- Must: pin fingerprints in CI (Debug = Release), count recomputed nodes and tiles in tests, show
  traces in the editor, document every node's version history.

## Evidence / benchmark
None yet. Part 1 pins fingerprints; part 2 measures cache hit rates and job overhead; part 3 measures
per-tile invalidation on the Hillside terrain.

## Migration or reversal strategy
Graphs are data with a format version (ADR-005 migrations). If the evaluator design fails, the node
functions and the `.noorgraph` format survive and a new evaluator can run them; if a scripted node
type is needed later (option B), it can be one node type with stated determinism limits.

## Reviewed by
Sage (proposal).
