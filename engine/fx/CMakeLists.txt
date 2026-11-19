# fx: NoorFX, particles and effects (Milestone 8B, ADR-035). Effect particles are simulated here on the CPU,
# deterministically; drawing them is the renderer's (part 2). A runtime system, like physics and audio.
noor_add_module(fx
    SOURCES
        src/particles.cpp
        src/fx_asset.cpp
        src/particle_system.cpp
    PUBLIC_DEPS
        noor::foundation
        noor::asset
        noor::scene
        noor::runtime
)
