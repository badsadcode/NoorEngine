# A moving sun (Milestone 8A, part 2): render_scenes/Atmosphere_Day drags the time of day from 5:00 to
# 21:00 over 8 seconds (Scripts/DayCycle.luau), so the live sky and its lighting are rebuilt on the GPU
# every frame. The same objects under a still sun (Atmosphere_Noon) run first, for comparison. The
# moving sun must show
#   - the sky rebuilt every frame the sun moves, the tables only once;
#   - the rebuild's own GPU time (the Environment pass) small, in its worst frame too;
#   - the whole frame's GPU time and the CPU's work per frame far inside a 60 FPS frame (16.7 ms);
#   - no errors (Vulkan validation off: it would measure the validation layers, not us).
# Wall-clock frame times are printed, not judged: on a desktop where someone works in another window,
# the same scene ran at a 16.7 ms median one time and 26-31 ms the next (a still sun too: measured while
# writing this test, docs/rendering/MEASUREMENTS.md), so they measure the desktop, not this engine.
#   -DPLAYER=<noor_player> -DPROJECT=<render_scenes> -DWORK=<folder>
file(MAKE_DIRECTORY "${WORK}")

# The integer part of a number of milliseconds (CMake compares whole numbers only).
function(whole out value)
    string(REGEX REPLACE "^([0-9]+).*" "\\1" n "${value}")
    set(${out} ${n} PARENT_SCOPE)
endfunction()

function(run scene report)
    file(REMOVE "${report}")
    execute_process(COMMAND "${PLAYER}" "${PROJECT}" --scene ${scene} --frames 540 --no-audio --no-input
                            --report "${report}" --log "${report}.log"
                    RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
    if(NOT code EQUAL 0 OR NOT EXISTS "${report}")
        message(FATAL_ERROR "noor_player failed on ${scene} (${code}); see ${report}.log")
    endif()
endfunction()

run(Scenes/Atmosphere_Noon.noorscene "${WORK}/still.json")
run(Scenes/Atmosphere_Day.noorscene "${WORK}/moving.json")
file(READ "${WORK}/still.json" still)
file(READ "${WORK}/moving.json" json)
string(JSON still_median GET "${still}" timing_ms wall median)
string(JSON still_p95 GET "${still}" timing_ms wall p95)
string(JSON errors GET "${json}" errors)
string(JSON tables GET "${json}" sky_builds tables)
string(JSON skies GET "${json}" sky_builds sky)
string(JSON wall_median GET "${json}" timing_ms wall median)
string(JSON wall_p95 GET "${json}" timing_ms wall p95)
string(JSON wall_max GET "${json}" timing_ms wall max)
string(JSON env_median GET "${json}" timing_ms environment_gpu median)
string(JSON env_max GET "${json}" timing_ms environment_gpu max)
string(JSON gpu_p95 GET "${json}" timing_ms gpu p95)
string(JSON record_p95 GET "${json}" timing_ms record p95)
string(JSON game_p95 GET "${json}" timing_ms game p95)
string(JSON view_p95 GET "${json}" timing_ms view p95)
message(STATUS "still sun: frame ${still_median} ms median, ${still_p95} p95")
message(STATUS "day cycle: ${skies} sky rebuilds, ${tables} table builds; frame ${wall_median} ms median, ${wall_p95} p95, "
               "${wall_max} max; sky rebuild GPU ${env_median} ms median, ${env_max} max; whole frame GPU ${gpu_p95} ms p95; "
               "CPU p95: game ${game_p95}, view ${view_p95}, record ${record_p95} ms")
if(NOT errors EQUAL 0)
    message(FATAL_ERROR "${errors} error(s) logged; see ${WORK}/moving.json.log")
endif()
if(NOT tables EQUAL 1)
    message(FATAL_ERROR "the atmosphere's tables were built ${tables} times (the settings don't change: once)")
endif()
if(skies LESS 450)  # the sweep lasts 8 s (480 frames at 60 FPS), then the sun stays put
    message(FATAL_ERROR "only ${skies} sky rebuilds: the sun isn't moving the sky")
endif()
string(JSON gpu_median GET "${json}" timing_ms gpu median)
whole(gpu_median_ms "${gpu_median}")
if(gpu_median_ms GREATER 3)
    message(FATAL_ERROR "the whole frame takes ${gpu_median} ms on the GPU (median; budget for this small scene: under 4 ms)")
endif()
# A rebuild is about 0.2 ms at full GPU clocks; a frame whose GPU is still clocked down (or shared with
# another program) can take ten times that: the worst frame must stay well inside the budget anyway.
whole(env_median_ms "${env_median}")
whole(env_max_ms "${env_max}")
if(NOT env_median_ms EQUAL 0)
    message(FATAL_ERROR "a sky rebuild takes ${env_median} ms on the GPU (median; budget: under 1 ms)")
endif()
if(env_max_ms GREATER 4)
    message(FATAL_ERROR "a sky rebuild took ${env_max} ms on the GPU in its worst frame (budget: under 5 ms)")
endif()
whole(game_ms "${game_p95}")
whole(view_ms "${view_p95}")
whole(record_ms "${record_p95}")
math(EXPR cpu_ms "${game_ms} + ${view_ms} + ${record_ms}")
if(cpu_ms GREATER 4)
    message(FATAL_ERROR "the CPU's work per frame (p95: game ${game_p95}, view ${view_p95}, record ${record_p95} ms) is over 5 ms")
endif()
