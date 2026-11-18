# Terrain (ADR-031, Milestone 6): heightfield tiles, deterministic generation, tile meshes.
# Runtime-safe: games generate the same terrain from the same settings.
noor_add_module(terrain
    SOURCES
        src/terrain.cpp
    PUBLIC_DEPS
        noor::foundation
        noor::asset
        noor::render
    PRIVATE_DEPS
        stb::image
)
