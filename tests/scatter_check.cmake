# Scatter (Milestone 7, part 7): the test forest's graph makes hills and plants oaks on them; noor_player
# draws the cooked result (tests/data/forest_project, Scenes/Scattered.noorscene) and its report must
# show the forest, batched, with no errors (Vulkan validation on).
#   -DPLAYER=<noor_player> -DPROJECT=<forest_project> -DWORK=<folder>
file(MAKE_DIRECTORY "${WORK}")
set(report "${WORK}/report.json")
file(REMOVE "${report}")
execute_process(COMMAND "${PLAYER}" "${PROJECT}" --scene Scenes/Scattered.noorscene --frames 30 --no-audio --no-input --validation
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
message(STATUS "scattered forest: ${instances} objects drawn in ${draws} draw calls (${shadow_draws} for shadows)")
if(NOT errors EQUAL 0)
    message(FATAL_ERROR "${errors} error(s) logged; see ${WORK}/player.log")
endif()
if(instances LESS 100)
    message(FATAL_ERROR "only ${instances} objects drawn: the scattered forest is missing (was the graph cooked?)")
endif()
math(EXPR scene_draws "${draws} - ${shadow_draws}")
if(scene_draws GREATER 20)
    message(FATAL_ERROR "${scene_draws} scene draw calls: scattered instances aren't batching")
endif()
