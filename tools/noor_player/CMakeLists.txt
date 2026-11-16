# noor_player: runs a game outside the editor (ADR-028). noor_cli build copies it next to the game.
add_executable(noor_player main.cpp)
target_link_libraries(noor_player PRIVATE noor::foundation noor::platform noor::render noor::asset noor::scene noor::runtime noor::game)
noor_set_target_options(noor_player)
set_target_properties(noor_player PROPERTIES FOLDER "Tools")
