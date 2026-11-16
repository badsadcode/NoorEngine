# game: everything a running game needs, shared by the editor's Play and noor_player (ADR-028).
noor_add_module(game
    SOURCES
        src/game.cpp
        src/geometry.cpp
        src/scene_renderer.cpp
    PUBLIC_DEPS
        noor::asset
        noor::scene
        noor::runtime
        noor::render
    PRIVATE_DEPS
        noor::physics
        noor::audio
        noor::scripting
)
