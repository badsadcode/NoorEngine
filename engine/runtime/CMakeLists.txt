# runtime: the running game world (ADR-027): game loop, systems, input, built-in behaviours.
# Physics, audio and scripting arrive as systems; this module does not depend on them.
noor_add_module(runtime
    SOURCES
        src/input.cpp
        src/world.cpp
        src/behaviours.cpp
    PUBLIC_DEPS
        noor::foundation
        noor::asset
        noor::scene
)
