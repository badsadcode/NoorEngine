add_executable(noor_cli main.cpp)
target_link_libraries(noor_cli PRIVATE noor::foundation noor::asset noor::scene)
noor_set_target_options(noor_cli)
set_target_properties(noor_cli PROPERTIES FOLDER "Tools")

# `noor_cli build` packs noor_player.exe and, for C# scripts, the managed half of scripting: keep
# current copies next to noor_cli. A separate target that runs every build (the copies are
# "if different"), so a rebuilt player is copied even when noor_cli itself didn't change.
set(payload_commands COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:noor_player> $<TARGET_FILE_DIR:noor_cli>)
if(TARGET noor_scripting_managed)
    list(APPEND payload_commands
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${NOOR_MANAGED_DIR} $<TARGET_FILE_DIR:noor_cli>/managed)
endif()
add_custom_target(noor_cli_payload ALL ${payload_commands} COMMENT "Copying noor_player (and managed scripting) next to noor_cli" VERBATIM)
add_dependencies(noor_cli_payload noor_cli noor_player)
if(TARGET noor_scripting_managed)
    add_dependencies(noor_cli_payload noor_scripting_managed)
endif()
set_target_properties(noor_cli_payload PROPERTIES FOLDER "Tools")
