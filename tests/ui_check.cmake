# Game UI from scripts (gameplay foundations, part 6b): tests/data/ui_project's menu script pauses the game with a menu
# on frame 10, writes into it, listens to it, ticks a checkbox and reads the controls back, clicks Resume on frame 20 (the
# click resumes the game), and keeps a HUD page (opened by a ui_document component) showing a moving box's position. The
# Lua scene in the project, the Lua scene in a built game and the C# scene in a built game must show exactly the same
# HUD at the end.
#   -DPLAYER=<noor_player> -DGAME=<the built game's .exe> -DPROJECT=<tests/data/ui_project> -DWORK=<folder>
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(expect "frame 40, paused false, menu false, box 0.483, hud 0.483 | change volume 0.25; volume 0.25, subtitles true, title Paused at 0.167; change subtitles true; click resume")
set(args --frames 40 --no-audio --no-input --windowed)

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
