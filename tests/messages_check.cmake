# Messages (gameplay foundations, part 3): tests/data/messages_project's Lua lever is pulled three times (a "pulled"
# message each, with its strength), the door listening to it calls the lever's "status" back, and after 2 s the lever
# calls the door's "is_open". The door is Lua in Scenes/Main and C# in Scenes/Main_Mixed: the project (Lua), a built
# game (Lua) and a built game (Lua lever, C# door) must all show the same HUD.
#   -DPLAYER=<noor_player> -DGAME=<the built game's .exe> -DPROJECT=<tests/data/messages_project> -DWORK=<folder>
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(expect0 "lever pulls 3, door open true")
set(expect1 "heard 3, strength 3, from Lever, lever says ok 3")
set(args --frames 150 --no-audio --no-input --windowed)

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
    if(NOT line0 STREQUAL expect0 OR NOT line1 STREQUAL expect1)
        message(FATAL_ERROR "${name}: expected '${expect0}' / '${expect1}'")
    endif()
endfunction()

play(project_lua "${PLAYER}" "${PROJECT}" --scene Scenes/Main.noorscene)
play(game_lua "${GAME}" --scene Scenes/Main.noorscene)
play(game_mixed "${GAME}" --scene Scenes/Main_Mixed.noorscene)
