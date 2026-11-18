# A sculpted hill in a built game (Milestone 6, part 6; tests/data/sculpt_project built by noor_cli): a
# flat terrain at 0 m whose only shape is its sculpt layer, a smooth hill 6 m tall and 20 m in radius at
# the centre. A character walks from x = -30 towards +x at 3 m/s for 10 s: it must end near the top of
# the hill, standing on it. That only works if the .noorsculpt file was packed, loaded and collided with.
#   -DPLAYER=<built Sculpt Test.exe> -DWORK=<folder>
file(MAKE_DIRECTORY "${WORK}")
set(report "${WORK}/report.json")
file(REMOVE "${report}")
execute_process(COMMAND "${PLAYER}" --frames 600 --no-audio --no-input --validation
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
if(x_m LESS -4 OR x_m GREATER 4)
    message(FATAL_ERROR "the walker isn't at the top of the hill (x = ${x}, expected about 0: from -30 at 3 m/s for 10 s)")
endif()
# Within 4 m of the top the hill is between 5.43 m (at 4 m) and 6 m high.
if(y_mm LESS 5380 OR y_mm GREATER 6050)
    message(FATAL_ERROR "the walker's feet aren't on the sculpted hill (y = ${y_mm} mm; the hill there is 5430..6000 mm)")
endif()
message(STATUS "feet at ${y_mm} mm on the sculpted hill")
