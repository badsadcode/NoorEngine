# NoorEngine Devlog #039 — Nodes

**Date:** 2026-11-18
**Build / commit:** Milestone 7, part 1

## This week we actually did

**The first piece of the procedural graph.** Milestone 7 is about generating things: terrain,
masks, and later forests. Before any clever generator, the engine needs one shared machine that runs
them all the same way. Part 1 is that machine's heart.

- **A graph is a file.** A `.noorgraph` lists nodes ("make hills", "clamp to 25 m"), the links
  between them, and what comes out. Save it twice and you get the same bytes twice.
- **Nodes are typed, with units.** A height field is in metres, a mask is 0 to 1, and you can't plug
  one into the other by accident. The engine says so, on the right node.
- **Six nodes to start:** constant, math, noise, combine, clamp and remap. The noise node uses the
  terrain's own noise, so a graph and a terrain with the same seed make the same hills, down to the
  last bit (a test checks every sample).
- **Every result has a fingerprint.** A test pins one for a six-node graph, and it's identical in
  Debug and Release builds.

No pictures this week: there's nothing to look at yet, only things to trust. The graph editor
comes in part 4.

## Build of the Week 🏆

**Randomness that belongs to its node.** Each node's random numbers come from the graph's seed plus
the node's own name. So adding a node, deleting one, or shuffling their order never changes what
the *other* nodes make. That matters for the milestone's big promise: hand-made changes survive
when you regenerate. If touching one node reshuffled the whole world, nothing would survive. A test
adds a node, reverses the list, and checks that nothing else moves.

## Sage vs. Reality

**The innocent bystander.** The first "mistakes" test linked a node to an output that `hills`
doesn't have. The engine blamed `hills`, and that stopped the whole healthy chain behind it. A bad
link is the problem of whoever receives it, not of the node it points at. Now the receiving node gets
the message, and `hills` carries on.

**Units you only know later.** A `constant` picks its unit with a parameter, so "metres plus a ratio"
can't always be caught before running. The first version missed that case entirely; a test
written to prove it was caught proved the opposite. Now units are checked twice: once before
running (whatever is known), and again as each node runs.

## Next engineering target
**Part 2: cache, jobs and `noor_cli generate`.** Results stored under those fingerprints, only
changed nodes recomputed, work on background threads with progress and Cancel, and the same graph
from the command line producing the same bytes as the editor.

## Bad Ideas Works Award for Questionable Decisions
**Winner:** Naming every node so that its name is also its random seed.
**Defence:** Rename a node and its hills move. That's not a bug, that's a feature with a warning label.
