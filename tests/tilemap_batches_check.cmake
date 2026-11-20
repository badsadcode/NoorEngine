# Tilemaps are drawn in chunks (2D foundations, part 4): the 256 x 256 map (tests/data/render_scenes Tilemap_Big, about
# 46,000 tiles) must take one draw per 32 x 32 chunk (64), not one per tile, and must not rebuild its meshes every
# frame (its CPU per frame stays small).
#   -DPLAYER=<noor_player> -DPROJECT=<render_scenes> -DWORK=<folder>
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${PLAYER}" "${PROJECT}" --scene Scenes/Tilemap_Big.noorscene --frames 180 --no-audio --no-input
                        --report "${WORK}/report.json" --log "${WORK}/player.log" RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
if(NOT code EQUAL 0 OR NOT EXISTS "${WORK}/report.json")
    message(FATAL_ERROR "noor_player failed (${code}); see ${WORK}/player.log")
endif()
file(READ "${WORK}/report.json" r)
string(JSON draws GET "${r}" render draw_calls)
string(JSON triangles GET "${r}" render triangles)
string(JSON cpu GET "${r}" timing_ms cpu median)
string(JSON errors GET "${r}" errors)
message(STATUS "Tilemap_Big: ${draws} draw calls, ${triangles} triangles, CPU ${cpu} ms a frame (median)")
if(NOT errors EQUAL 0)
    message(FATAL_ERROR "errors were logged: see ${WORK}/player.log")
endif()
# 64 chunks, plus the backdrop and the rest of the frame's few draws.
if(draws LESS 64 OR draws GREATER 72)
    message(FATAL_ERROR "the big map should take one draw per chunk (64, and a few others): ${draws}")
endif()
if(triangles LESS 90000)
    message(FATAL_ERROR "the big map's tiles aren't all drawn (${triangles} triangles)")
endif()
# Rebuilding 64 chunk meshes every frame would cost several milliseconds.
if(cpu GREATER 3)
    message(FATAL_ERROR "the big map costs ${cpu} ms of CPU a frame: are its chunks rebuilt every frame?")
endif()
