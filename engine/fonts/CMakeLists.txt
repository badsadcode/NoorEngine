# The built-in font (Atkinson Hyperlegible, SIL OFL 1.1) as a byte array: the HUD's (engine/game) and game UI's
# (engine/ui) default, so every game has it. The header defines it `inline constexpr`: one copy in a program however
# many modules include it. Licence: third_party/fonts/OFL.txt (noor_cli build copies it into games).
set(font_header ${CMAKE_CURRENT_BINARY_DIR}/generated/noor/fonts/hud_font.h)
add_custom_command(
    OUTPUT ${font_header}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated/noor/fonts
    COMMAND ${CMAKE_COMMAND} -DINPUT=${NOOR_HUD_FONT} -DOUTPUT=${font_header} -DSYMBOL=k_hud_font
            -P ${PROJECT_SOURCE_DIR}/cmake/NoorEmbedBinary.cmake
    DEPENDS ${NOOR_HUD_FONT} ${PROJECT_SOURCE_DIR}/cmake/NoorEmbedBinary.cmake
    COMMENT "Embedding the built-in font"
    VERBATIM)
add_custom_target(noor_builtin_font_header DEPENDS ${font_header})
set_target_properties(noor_builtin_font_header PROPERTIES FOLDER "Engine")
add_library(noor_builtin_font INTERFACE)
add_library(noor::builtin_font ALIAS noor_builtin_font)
target_include_directories(noor_builtin_font INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_dependencies(noor_builtin_font noor_builtin_font_header)
