# physics: Jolt Physics (ADR-010) as a runtime system (ADR-027).
noor_add_module(physics
    SOURCES
        src/physics.cpp
    PUBLIC_DEPS
        noor::runtime
    PRIVATE_DEPS
        Jolt
)
