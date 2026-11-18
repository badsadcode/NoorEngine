# Instanced drawing and levels of detail (Milestone 7, part 6, ADR-032): noor_player draws the test
# forest (tests/data/forest_project: 120 NoorForm oaks, near and far) and its report must show
#   - batching: far fewer GPU draw calls than objects drawn (one draw per mesh range and material);
#   - culling: objects outside the view skipped;
#   - levels of detail: far fewer triangles than 120 full-detail oaks (15,268 each, about 1.8 million);
#   - no errors (Vulkan validation on).
#   -DPLAYER=<noor_player> -DPROJECT=<forest_project> -DWORK=<folder>
file(MAKE_DIRECTORY "${WORK}")
set(report "${WORK}/report.json")
file(REMOVE "${report}")
execute_process(COMMAND "${PLAYER}" "${PROJECT}" --scene Scenes/Forest.noorscene --frames 30 --no-audio --no-input --validation
                        --report "${report}" --log "${WORK}/player.log"
                RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
if(NOT code EQUAL 0 OR NOT EXISTS "${report}")
    message(FATAL_ERROR "noor_player failed (${code}); see ${WORK}/player.log")
endif()
file(READ "${report}" json)
string(JSON errors GET "${json}" errors)
string(JSON draws GET "${json}" render draw_calls)
string(JSON shadow_draws GET "${json}" render shadow_draw_calls)
string(JSON instances GET "${json}" render instances)
string(JSON culled GET "${json}" render culled)
string(JSON triangles GET "${json}" render triangles)
message(STATUS "forest: ${instances} objects drawn in ${draws} draw calls (${shadow_draws} for shadows), ${culled} culled, ${triangles} triangles")
if(NOT errors EQUAL 0)
    message(FATAL_ERROR "${errors} error(s) logged; see ${WORK}/player.log")
endif()
if(instances LESS 100)
    message(FATAL_ERROR "only ${instances} objects drawn: the forest is missing")
endif()
math(EXPR scene_draws "${draws} - ${shadow_draws}")
if(scene_draws GREATER 20)
    message(FATAL_ERROR "${scene_draws} scene draw calls for ${instances} objects: instancing isn't batching")
endif()
if(culled LESS 1)
    message(FATAL_ERROR "nothing culled: the camera can't see the whole forest, so culling isn't working")
endif()
if(triangles GREATER 700000)
    message(FATAL_ERROR "${triangles} triangles: levels of detail aren't lowering far trees")
endif()
