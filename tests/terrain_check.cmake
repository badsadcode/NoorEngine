# Terrain in a built game (Milestone 6, part 5; tests/data/terrain_project built by noor_cli): one tile
# from a 16-bit ramp heightmap (0 m at x = -128 to 20 m at x = +128). A character walks up the ramp
# for 4 s; its feet must stay on the ground, y = (x + 128) / 256 * 20.
#   -DPLAYER=<built Terrain Test.exe> -DWORK=<folder>
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
string(JSON x GET "${json}" entities Walker 0)
string(JSON y GET "${json}" entities Walker 1)
message(STATUS "the walker ended at x = ${x}, y = ${y}")
if(NOT errors EQUAL 0)
    message(FATAL_ERROR "${errors} error(s) logged (see ${WORK}/player.log)")
endif()

# CMake's maths is integer-only: whole metres of x, millimetres of y.
string(REGEX MATCH "^-?[0-9]+" x_m "${x}")
string(REGEX MATCH "^-?[0-9]+" y_int "${y}")
string(REGEX MATCH "\\.([0-9]*)" y_dot "${y}")
string(SUBSTRING "${CMAKE_MATCH_1}000" 0 3 y_frac)
math(EXPR y_mm "${y_int} * 1000 + ${y_frac}")
if(x_m LESS -52 OR x_m GREATER -44)
    message(FATAL_ERROR "the walker didn't walk up the ramp (x = ${x}, expected about -48: from -60 at 3 m/s for 4 s)")
endif()
# The ramp rises 78 mm per metre; x is known to a metre here, so the ground lies between the heights at
# x - 1 and x + 1 (plus 5 cm: Jolt stores height fields slightly compressed).
math(EXPR ground_lo "(${x_m} - 1 + 128) * 20000 / 256 - 50")
math(EXPR ground_hi "(${x_m} + 1 + 128) * 20000 / 256 + 50")
message(STATUS "feet at ${y_mm} mm; the ground there is between ${ground_lo} and ${ground_hi} mm")
if(y_mm LESS ground_lo OR y_mm GREATER ground_hi)
    message(FATAL_ERROR "the walker's feet aren't on the ramp (y = ${y_mm} mm, the ground is ${ground_lo}..${ground_hi} mm)")
endif()
