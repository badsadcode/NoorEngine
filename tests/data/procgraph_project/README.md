# Graph Test

Test data for Milestone 7, part 2: `Assets/Graphs/hills.noorgraph`, the same six-node graph as
`tests/unit/test_procgraph.cpp` (its fingerprint is pinned there). `noor_cli generate` and the editor API both
run it; a test checks they write the same bytes.

`Scenes/Main.noorscene` has a terrain whose base is that graph (`"base": "graph"`) and a walker dropped from
30 m: in a built game (`cli.build_graph_terrain`, `player.graph_terrain`) it must land on the cooked terrain.
