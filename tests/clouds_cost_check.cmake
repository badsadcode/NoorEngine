# What clouds cost (Milestone 8A, part 4): render_scenes/Atmosphere_Clouds (two layers drifting with the wind:
# drawn every frame, the lighting rebuilt in steps as they drift) against Atmosphere_Noon (the same objects, no clouds). The plan's
# budget: clouds under 0.5 ms of GPU time a frame. Measured as the difference of the whole frame's GPU time in
# each run's fastest frame (sky pass, cloud shadows in the scene pass), validation off. Not the median: on a desktop
# whose GPU idles in a low power state and is shared with other programs, the median swings 3-10x between runs
# (seen while writing this: -875 to +878 us for the same pair); the fastest frame is the work itself.
#   -DPLAYER=<noor_player> -DPROJECT=<render_scenes> -DWORK=<folder>
file(MAKE_DIRECTORY "${WORK}")
function(run scene report)
    file(REMOVE "${report}")
    execute_process(COMMAND "${PLAYER}" "${PROJECT}" --scene ${scene} --frames 300 --no-audio --no-input --report "${report}" --log "${report}.log"
                    RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
    if(NOT code EQUAL 0 OR NOT EXISTS "${report}")
        message(FATAL_ERROR "noor_player failed on ${scene} (${code}); see ${report}.log")
    endif()
endfunction()
run(Scenes/Atmosphere_Noon.noorscene "${WORK}/clear.json")
run(Scenes/Atmosphere_Clouds.noorscene "${WORK}/clouds.json")
file(READ "${WORK}/clear.json" clear)
file(READ "${WORK}/clouds.json" clouds)
string(JSON clear_gpu GET "${clear}" timing_ms gpu min)
string(JSON clouds_gpu GET "${clouds}" timing_ms gpu min)
string(JSON clouds_median GET "${clouds}" timing_ms gpu median)
string(JSON env GET "${clouds}" timing_ms environment_gpu median)
string(JSON skies GET "${clouds}" sky_builds sky)
string(JSON errors GET "${clouds}" errors)
foreach(v clear_gpu clouds_gpu env)
    string(REGEX REPLACE "^([0-9]+)[.]?([0-9]*).*" "\\1" whole "${${v}}")
    string(REGEX REPLACE "^([0-9]+)[.]?([0-9]*).*" "\\2000" frac "${${v}}")
    string(SUBSTRING "${frac}" 0 3 frac)
    math(EXPR ${v}_us "${whole} * 1000 + ${frac}")
endforeach()
math(EXPR extra_us "${clouds_gpu_us} - ${clear_gpu_us}")
message(STATUS "clouds: fastest GPU frame ${clouds_gpu} ms vs ${clear_gpu} ms without (+${extra_us} us; median with clouds ${clouds_median}); "
               "a lighting rebuild ${env} ms; ${skies} sky rebuilds")
if(NOT errors EQUAL 0)
    message(FATAL_ERROR "${errors} error(s) logged; see ${WORK}/clouds.json.log")
endif()
# The lighting follows drifting clouds in steps (1% of their size: here every 2-3 seconds per copy), not every frame.
if(skies LESS 3 OR skies GREATER 60)
    message(FATAL_ERROR "${skies} sky rebuilds in 300 frames: the lighting should follow the drift in steps (a few, not every frame)")
endif()
if(extra_us GREATER 500)
    message(FATAL_ERROR "clouds cost ${extra_us} us a frame (budget: 500)")
endif()
