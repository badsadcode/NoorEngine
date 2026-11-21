# Time (gameplay foundations, part 2): tests/data/time_project runs a timer, a repeating timer that stops itself and a
# sequence that waits twice, while a script that runs while paused pauses the game for half a real second (checking the
# falling box doesn't move) and then plays on at half speed. The Lua scene in the project, the Lua scene in a built
# game and the C# scene in a built game must all show exactly the same HUD, and the right numbers.
#   -DPLAYER=<noor_player> -DGAME=<the built game's .exe> -DPROJECT=<tests/data/time_project> -DWORK=<folder>
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(expect0 "after 0.500, ticks 4, waits 0.300 0.800, updates 151")
set(expect1 "paused 0.500 real s, frozen true, scale 0.50, game 1.742, real 2.983")
set(args --frames 180 --no-audio --no-input --windowed)

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
play(game_csharp "${GAME}" --scene Scenes/Main_CSharp.noorscene)
