# Timelines (gameplay foundations, part 5): tests/data/timeline_project's door plays its "open" timeline from a
# script (a hinge with back_out, a light, a call and a message, at 1.5x speed). The Lua scene in the project, the Lua
# scene in a built game and the C# scene in a built game must show exactly the same HUD at the end.
#   -DPLAYER=<noor_player> -DGAME=<the built game's .exe> -DPROJECT=<tests/data/timeline_project> -DWORK=<folder>
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(expect "playing nil, hinge 100.00, light 600.0, creak x2, latched click, end open")
set(args --frames 120 --no-audio --no-input --windowed)

function(play name exe)
    execute_process(COMMAND "${exe}" ${ARGN} ${args} --report "${WORK}/${name}.json" --log "${WORK}/${name}.log"
                    RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
    if(NOT code EQUAL 0 OR NOT EXISTS "${WORK}/${name}.json")
        message(FATAL_ERROR "${name}: the player failed (${code}); see ${WORK}/${name}.log")
    endif()
    file(READ "${WORK}/${name}.json" report)
    string(JSON errors GET "${report}" errors)
    string(JSON line GET "${report}" hud_text 0)
    message(STATUS "${name}: '${line}'")
    if(NOT errors EQUAL 0)
        message(FATAL_ERROR "${name}: ${errors} error(s) logged; see ${WORK}/${name}.log")
    endif()
    if(NOT line STREQUAL expect)
        message(FATAL_ERROR "${name}: expected '${expect}'")
    endif()
endfunction()

play(project_lua "${PLAYER}" "${PROJECT}" --scene Scenes/Main.noorscene)
play(game_lua "${GAME}" --scene Scenes/Main.noorscene)
play(game_csharp "${GAME}" --scene Scenes/Main_CSharp.noorscene)
