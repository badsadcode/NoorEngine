# game: everything a running game needs, shared by the editor's Play and noor_player (ADR-028).
noor_add_module(game
    SOURCES
        src/game.cpp
        src/geometry.cpp
        src/hud.cpp
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
        stb::truetype
)

# The HUD font (Atkinson Hyperlegible, SIL OFL 1.1) built into the engine as a byte array, so every
# game has it. Licence: third_party/fonts/OFL.txt (noor_cli build copies it into games).
set(font_header ${CMAKE_CURRENT_BINARY_DIR}/generated/noor/fonts/hud_font.h)
add_custom_command(
    OUTPUT ${font_header}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated/noor/fonts
    COMMAND ${CMAKE_COMMAND} -DINPUT=${NOOR_HUD_FONT} -DOUTPUT=${font_header} -DSYMBOL=k_hud_font
            -P ${PROJECT_SOURCE_DIR}/cmake/NoorEmbedBinary.cmake
    DEPENDS ${NOOR_HUD_FONT} ${PROJECT_SOURCE_DIR}/cmake/NoorEmbedBinary.cmake
    COMMENT "Embedding the HUD font"
    VERBATIM)
target_sources(noor_game PRIVATE ${font_header})
target_include_directories(noor_game PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
