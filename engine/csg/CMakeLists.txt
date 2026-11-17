# CSG brushes (ADR-016, Milestone 6): brush shapes, booleans (Manifold), materials and UVs per
# source face. Editor and cooking only; nothing in the runtime or the player links this.
noor_add_module(csg
    SOURCES
        src/brush.cpp
        src/model.cpp
    PUBLIC_DEPS
        noor::foundation
    PRIVATE_DEPS
        manifold
)
