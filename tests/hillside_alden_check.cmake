# The 8C sample in a built game (Milestone 8C, part 8): samples/Hillside's Hillside_Alden (Alden Wayfarer wandering a
# loop by the house on root motion: walking, running a stretch, resting, walking on; his staff on a socket) runs 900
# frames (fifteen seconds: past the run and into the rest) in the project and in the built Hillside game, and must
# play the same: every bone posed the same (the animator's fingerprint), Alden in the same place to the millimetre.
#   -DPLAYER=<noor_player> -DGAME=<the built Hillside.exe> -DPROJECT=<samples/Hillside> -DWORK=<folder>
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(args --scene Scenes/Hillside_Alden.noorscene --frames 900 --no-audio --no-input)
execute_process(COMMAND "${PLAYER}" "${PROJECT}" ${args} --report "${WORK}/project.json" --log "${WORK}/project.log"
                RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
if(NOT code EQUAL 0 OR NOT EXISTS "${WORK}/project.json")
    message(FATAL_ERROR "noor_player failed on the sample (${code}); see ${WORK}/project.log")
endif()
execute_process(COMMAND "${GAME}" ${args} --report "${WORK}/game.json" --log "${WORK}/game.log" RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
if(NOT code EQUAL 0 OR NOT EXISTS "${WORK}/game.json")
    message(FATAL_ERROR "the built Hillside game failed (${code}); see ${WORK}/game.log")
endif()
file(READ "${WORK}/project.json" a)
file(READ "${WORK}/game.json" b)
foreach(side a b)
    string(JSON ${side}_print GET "${${side}}" animation fingerprint)
    string(JSON ${side}_bones GET "${${side}}" animation bones)
    string(JSON ${side}_state GET "${${side}}" animation entities 0 animation)
    string(JSON ${side}_at GET "${${side}}" animation entities 0 position)
    string(JSON ${side}_errors GET "${${side}}" errors)
endforeach()
message(STATUS "project:    ${a_bones} bones, state ${a_state}, Alden at ${a_at}, fingerprint ${a_print}")
message(STATUS "built game: ${b_bones} bones, state ${b_state}, Alden at ${b_at}, fingerprint ${b_print}")
if(NOT a_errors EQUAL 0 OR NOT b_errors EQUAL 0)
    message(FATAL_ERROR "errors were logged: see ${WORK}/project.log and ${WORK}/game.log")
endif()
# He set off from (22, -8.5) and has been round two corners: well away from the start.
string(REPLACE " " ";" at "${a_at}")
list(GET at 0 x)
list(GET at 2 z)
# 11 bones: his idle, walk and run move 9; his swim stroke (8D part 8, a state of the same controller) the neck and head too.
if(NOT a_state STREQUAL "Move" OR NOT a_bones EQUAL 11 OR NOT x GREATER 28)
    message(FATAL_ERROR "Alden isn't wandering in the project (state Move, the 11 bones his controller's clips move, past x = 28 expected; at ${a_at})")
endif()
if(NOT a_print STREQUAL b_print OR NOT a_at STREQUAL b_at)
    message(FATAL_ERROR "the built game's Alden differs from the project's")
endif()
