# The Milestone 6 sample in a built game (samples/Hillside built by noor_cli): the walker starts on the
# dirt path 40 m south of the house and walks north at 3 m/s for 20 s. It must climb the path, go
# through the arched doorway and end up inside the house, standing on its floor: the terrain, its
# sculpting, the cooked CSG house and its collision all have to work together for that.
#   -DPLAYER=<built Hillside.exe> -DWORK=<folder>
file(MAKE_DIRECTORY "${WORK}")
set(report "${WORK}/report.json")
file(REMOVE "${report}")
execute_process(COMMAND "${PLAYER}" --frames 1200 --no-audio --no-input --validation
                        --report "${report}" --screenshot "${WORK}/last_frame.bmp" --log "${WORK}/player.log"
                RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "the game exited with ${code} (see ${WORK}/player.log)")
endif()
file(READ "${report}" json)
string(JSON errors GET "${json}" errors)
string(JSON x GET "${json}" entities Walker 0)
string(JSON y GET "${json}" entities Walker 1)
string(JSON z GET "${json}" entities Walker 2)
message(STATUS "the walker ended at (${x}, ${y}, ${z})")
if(NOT errors EQUAL 0)
    message(FATAL_ERROR "${errors} error(s) logged (see ${WORK}/player.log)")
endif()
# Whole metres are enough: the house's inside is x 16.3..23.7, z -2.7..2.7 (outside walls 8 x 6 m
# around (20, 0)); its floor is above 5 m (the pad is at about 5.6 m).
string(REGEX MATCH "^-?[0-9]+" x_m "${x}")
string(REGEX MATCH "^-?[0-9]+" y_m "${y}")
string(REGEX MATCH "^-?[0-9]+" z_m "${z}")
if(x_m LESS 17 OR x_m GREATER 23 OR z_m LESS -2 OR z_m GREATER 2)
    message(FATAL_ERROR "the walker isn't inside the house (${x}, ${z}): stuck on the way, or walked through a wall")
endif()
if(y_m LESS 5 OR y_m GREATER 6)
    message(FATAL_ERROR "the walker isn't standing on the house's floor (y = ${y})")
endif()
message(STATUS "the walker followed the path into the house")
