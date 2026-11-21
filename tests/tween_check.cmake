# Tweens (gameplay foundations, part 4): tests/data/tween_project's door rises, grows with a bounce and turns red, each
# tween started by the one before's on_done, while a kinematic platform goes back and forth forever. The Lua scene in
# the project, the Lua scene in a built game and the C# scene in a built game must show exactly the same HUD.
#   -DPLAYER=<noor_player> -DGAME=<the built game's .exe> -DPROJECT=<tests/data/tween_project> -DWORK=<folder>
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(expect "steps 3, door y 3.000, scale 1.500, colour #FF0000, platform x -0.197")
set(args --frames 150 --no-audio --no-input --windowed)

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
