# noor_player: runs a game outside the editor (ADR-028). noor_cli build copies it next to the game.
add_executable(noor_player main.cpp)
target_link_libraries(noor_player PRIVATE noor::foundation noor::platform noor::render noor::asset noor::scene noor::runtime noor::scripting noor::game)
noor_set_target_options(noor_player)
set_target_properties(noor_player PROPERTIES FOLDER "Tools")
# A game, not a console program: no console window when it's double-clicked (main() stays the entry
# point). The log goes to <exe name>.log next to the executable (or --log <file>).
if(MSVC)
    target_link_options(noor_player PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()
