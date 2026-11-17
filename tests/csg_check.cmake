# The Milestone 6 runtime gate, in a built game (tests/data/csg_project, built by noor_cli, which
# cooks its CSG model): a wall with an arched doorway; one character walks through the doorway,
# the other into the wall. Checks collision (the model's cooked mesh collider), drawing (the wall
# fills the middle of the picture) and Vulkan validation.
#   -DPLAYER=<built CSG Test.exe> -DWORK=<folder>
file(MAKE_DIRECTORY "${WORK}")
set(report "${WORK}/report.json")
file(REMOVE "${report}")
execute_process(COMMAND "${PLAYER}" --frames 240 --no-audio --no-input --validation
                        --report "${report}" --screenshot "${WORK}/last_frame.bmp" --log "${WORK}/player.log"
                RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "the game exited with ${code} (see ${WORK}/player.log)")
endif()
file(READ "${report}" json)
string(JSON errors GET "${json}" errors)
string(JSON through GET "${json}" entities "Through the door" 2)
string(JSON blocked GET "${json}" entities "Into the wall" 2)
string(JSON r GET "${json}" center_pixel 0)
string(JSON g GET "${json}" center_pixel 1)
string(JSON b GET "${json}" center_pixel 2)
message(STATUS "4 s of walking at 2 m/s from z = 3: through the door z = ${through}, into the wall z = ${blocked}; centre pixel ${r} ${g} ${b}")
if(NOT errors EQUAL 0)
    message(FATAL_ERROR "${errors} error(s) logged (see ${WORK}/player.log)")
endif()
# The wall is at z = 0 (0.3 m thick): the walker in the doorway ends far beyond it (z = -5),
# the other is stopped in front of it (its 0.3 m radius from the wall's face at z = 0.15).
if(through GREATER -4)
    message(FATAL_ERROR "the walker didn't get through the doorway (z = ${through})")
endif()
if(blocked LESS 0.3 OR blocked GREATER 0.6)
    message(FATAL_ERROR "the wall didn't stop the other walker where it should (z = ${blocked}, expected about 0.47)")
endif()
# The wall (white default material, lit) covers the middle of the picture: not the purple floor or sky.
if(r LESS 150 OR g LESS 150 OR b LESS 150)
    message(FATAL_ERROR "the middle of the picture isn't the wall (${r} ${g} ${b}): the cooked model wasn't drawn")
endif()
