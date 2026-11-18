# The procedural graph (Milestone 7, ADR-014): graph files, typed nodes, deterministic evaluation.
# Editor and tools only: the runtime-boundary test keeps it out of games.
noor_add_module(procgraph
    SOURCES
        src/procgraph.cpp
        src/nodes.cpp
    PUBLIC_DEPS
        noor::foundation
        noor::asset
    PRIVATE_DEPS
        noor::terrain
)
