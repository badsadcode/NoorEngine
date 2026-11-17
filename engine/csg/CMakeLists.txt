# CSG brushes (ADR-016, Milestone 6): brush shapes, booleans (Manifold), materials and UVs per
# source face; reading brushes from scenes and cooking models for games. Editor and tools only:
# nothing in the runtime or the player links this (tests/runtime_boundary.cmake checks).
noor_add_module(csg
    SOURCES
        src/brush.cpp
        src/model.cpp
        src/scene_csg.cpp
    PUBLIC_DEPS
        noor::foundation
        noor::scene
        noor::game
    PRIVATE_DEPS
        manifold
)
