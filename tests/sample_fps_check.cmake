# NoorFPS-lite (samples/NoorFPS-lite), built by noor_cli: runs one scene for 15 s of game time with
# no input, so the game stays in its demo (it aims and shoots by itself), and reads the score off
# the HUD from the player's --report.
#   -DPLAYER=<built NoorFPS-lite.exe> -DSCENE=<Scenes/X.noorscene> -DWORK=<folder>
file(MAKE_DIRECTORY "${WORK}")
set(report "${WORK}/report.json")
file(REMOVE "${report}")
execute_process(COMMAND "${PLAYER}" --scene "${SCENE}" --frames 900 --no-audio --no-input
                        --report "${report}" --screenshot "${WORK}/last_frame.bmp" --log "${WORK}/player.log"
                RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "the game exited with ${code} (see ${WORK}/player.log)")
endif()
file(READ "${report}" json)
string(JSON frames GET "${json}" frames)
string(JSON errors GET "${json}" errors)
string(JSON first GET "${json}" hud_text 0)
message(STATUS "${SCENE}: ${frames} frames, ${errors} error(s), HUD says \"${first}\"")
if(NOT frames EQUAL 900)
    message(FATAL_ERROR "ran ${frames} frames, expected 900")
endif()
if(NOT errors EQUAL 0)
    message(FATAL_ERROR "${errors} error(s) logged (see ${WORK}/player.log)")
endif()
if(NOT first MATCHES "^DEMO · Hits: ([0-9]+)$")
    message(FATAL_ERROR "the HUD doesn't show the demo's score (\"${first}\")")
endif()
# The demo turns at 150 degrees/s and fires every 0.6 s when a target is under the crosshair:
# about 24 hits in 15 s. Fewer than 10 means aiming, shooting or hitting broke.
if(CMAKE_MATCH_1 LESS 10)
    message(FATAL_ERROR "the demo only hit ${CMAKE_MATCH_1} targets in 15 s (expected about 24)")
endif()
