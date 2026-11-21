# Spawning from scripts (gameplay foundations, part 1): tests/data/spawn_project spawns ten bullet prefabs, tags every
# other one, teleports the first back up and writes what it saw on the HUD, once from Lua and once from C#. The Lua
# scene in the project, the Lua scene in a built game and the C# scene in a built game must all see the same: ten
# bullets with their trails, five tagged, the teleport done, the first bullet in the same place (to the HUD's 1 cm).
#   -DPLAYER=<noor_player> -DGAME=<the built game's .exe> -DPROJECT=<tests/data/spawn_project> -DWORK=<folder>
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(expected "spawned 10, bullets 10, even 5, trail true, teleported true")
set(args --frames 120 --no-audio --no-input --windowed)

function(play name exe)
    execute_process(COMMAND "${exe}" ${ARGN} ${args} --report "${WORK}/${name}.json" --log "${WORK}/${name}.log"
                    RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
    if(NOT code EQUAL 0 OR NOT EXISTS "${WORK}/${name}.json")
        message(FATAL_ERROR "${name}: the player failed (${code}); see ${WORK}/${name}.log")
    endif()
    file(READ "${WORK}/${name}.json" report)
    string(JSON errors GET "${report}" errors)
    string(JSON line0 GET "${report}" hud_text 0)
    string(JSON line1 GET "${report}" hud_text 1)
    message(STATUS "${name}: '${line0}' / '${line1}'")
    if(NOT errors EQUAL 0)
        message(FATAL_ERROR "${name}: ${errors} error(s) logged; see ${WORK}/${name}.log")
    endif()
    if(NOT line0 STREQUAL expected)
        message(FATAL_ERROR "${name}: the HUD says '${line0}', expected '${expected}'")
    endif()
    set(${name}_first "${line1}" PARENT_SCOPE)
endfunction()

play(project_lua "${PLAYER}" "${PROJECT}" --scene Scenes/Main.noorscene)
play(game_lua "${GAME}" --scene Scenes/Main.noorscene)
play(game_csharp "${GAME}" --scene Scenes/Main_CSharp.noorscene)
if(NOT project_lua_first STREQUAL game_lua_first OR NOT project_lua_first STREQUAL game_csharp_first)
    message(FATAL_ERROR "the first bullet isn't in the same place: '${project_lua_first}', '${game_lua_first}', '${game_csharp_first}'")
endif()
