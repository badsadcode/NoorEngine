# A golden-image render test (Milestone 5): draws one scene of tests/data/render_scenes with
# noor_player and compares the picture with tests/golden/<NAME>.png using noor_image_compare.
#   -DPLAYER=<noor_player> -DCOMPARE=<noor_image_compare> -DPROJECT=<folder> -DSCENE=<Scenes/X.noorscene>
#   -DREFERENCE=<tests/golden/X.png> -DWORK=<folder for the screenshot and the diff>
# With the environment variable NOOR_UPDATE_GOLDEN=1 the screenshot becomes the new reference
# (look at it first!).
file(MAKE_DIRECTORY "${WORK}")
set(shot "${WORK}/screenshot.bmp")
file(REMOVE "${shot}")
execute_process(COMMAND "${PLAYER}" "${PROJECT}" --scene "${SCENE}" --frames 5 --no-audio --no-input
                        --screenshot "${shot}" --log "${WORK}/player.log"
                RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
if(NOT code EQUAL 0 OR NOT EXISTS "${shot}")
    message(FATAL_ERROR "noor_player failed (${code}); see ${WORK}/player.log")
endif()
if("$ENV{NOOR_UPDATE_GOLDEN}" STREQUAL "1")
    execute_process(COMMAND "${COMPARE}" "${shot}" "${REFERENCE}" --update RESULT_VARIABLE code)
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "couldn't write ${REFERENCE}")
    endif()
    message(STATUS "Reference updated: ${REFERENCE}")
    return()
endif()
execute_process(COMMAND "${COMPARE}" "${shot}" "${REFERENCE}" --diff "${WORK}/diff.png"
                RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
message(STATUS "${out}${err}")
if(NOT code EQUAL 0)
    message(FATAL_ERROR "the picture differs from ${REFERENCE}: see ${WORK}/screenshot.bmp and ${WORK}/diff.png")
endif()
