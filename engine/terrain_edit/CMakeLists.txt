# Terrain editing (Milestone 6, part 6; ADR-031): sculpting brushes that write the sculpt layer.
# Editor and tools only: the runtime-boundary test keeps it out of games.
noor_add_module(terrain_edit
    SOURCES
        src/brush.cpp
    PUBLIC_DEPS
        noor::terrain
)
