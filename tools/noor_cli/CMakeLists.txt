add_executable(noor_cli main.cpp)
target_link_libraries(noor_cli PRIVATE noor::foundation noor::asset noor::scene)
noor_set_target_options(noor_cli)
set_target_properties(noor_cli PROPERTIES FOLDER "Tools")
# `noor_cli build` packs noor_player.exe: keep a copy next to noor_cli.
add_dependencies(noor_cli noor_player)
add_custom_command(TARGET noor_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:noor_player> $<TARGET_FILE_DIR:noor_cli>)
