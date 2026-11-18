# A terrain built on a procedural graph, in a built game (Milestone 7, part 3; tests/data/procgraph_project
# built by noor_cli, which cooks the graph into the game): a walker dropped from 30 m above the centre must
# land on the cooked terrain. The graph's ground there is 9.33 m (its neighbouring samples 9.24..9.42 m;
# the graph's fingerprint is pinned in test_procgraph). Without the cooked field there'd be no ground at all.
#   -DPLAYER=<built Graph Test.exe> -DWORK=<folder>
file(MAKE_DIRECTORY "${WORK}")
set(report "${WORK}/report.json")
file(REMOVE "${report}")
execute_process(COMMAND "${PLAYER}" --frames 180 --no-audio --no-input --validation
                        --report "${report}" --screenshot "${WORK}/last_frame.bmp" --log "${WORK}/player.log"
                RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "the game exited with ${code} (see ${WORK}/player.log)")
endif()
file(READ "${report}" json)
string(JSON errors GET "${json}" errors)
string(JSON y GET "${json}" entities Walker 1)
message(STATUS "the walker ended at y = ${y}")
if(NOT errors EQUAL 0)
    message(FATAL_ERROR "${errors} error(s) logged (see ${WORK}/player.log)")
endif()
string(REGEX MATCH "^-?[0-9]+" y_int "${y}")
string(REGEX MATCH "\\.([0-9]*)" y_dot "${y}")
string(SUBSTRING "${CMAKE_MATCH_1}000" 0 3 y_frac)
math(EXPR y_mm "${y_int} * 1000 + ${y_frac}")
if(y_mm LESS 9200 OR y_mm GREATER 9480)
    message(FATAL_ERROR "the walker isn't standing on the graph's terrain (y = ${y_mm} mm; the ground there is about 9330 mm)")
endif()
message(STATUS "feet at ${y_mm} mm on the graph's terrain")
