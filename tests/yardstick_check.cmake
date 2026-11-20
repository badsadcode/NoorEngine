# The yardsticks (Milestone 9A, part 1): Milestone 9 adds big worlds, but small games must not pay for them (M9_PLAN.md,
# principle 0). Runs one yardstick project in noor_player and compares what it costs with the baseline measured when
# 9A started (tests/yardsticks.json, written by tools/samples/measure_yardsticks.py): the CPU per frame (median and
# 95th percentile of the game, the view and recording), the GPU per frame, memory (working set, private, GPU resident),
# threads, and how long the first frame took. Each has a limit in the baseline file (the measurement plus a margin for
# a busy machine); going over any fails. The time to the first frame is measured warm (after a short warm-up launch).
#   -DPLAYER=<noor_player> -DPROJECT=<folder> -DSCENE=<scene> -DNAME=<baseline entry> -DBASELINE=<yardsticks.json>
#   -DWORK=<folder> [-DSTALL_MS=<ms>]  (a deliberate stall in every update: proves the check catches one)
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
file(READ "${BASELINE}" baseline)
string(JSON entry ERROR_VARIABLE missing GET "${baseline}" projects ${NAME})
if(missing)
    message(FATAL_ERROR "no baseline for '${NAME}' in ${BASELINE}")
endif()
string(JSON frames GET "${entry}" frames)
# A short warm-up launch first, so "time to the first frame" is measured warm: right after a C# build or with the disk
# cache cold, one launch can take seconds longer (measured: 0.7 s warm, 9 s straight after cli.build_sample_fps), which
# says nothing about the engine.
execute_process(COMMAND "${PLAYER}" "${PROJECT}" --scene "${SCENE}" --frames 30 --no-audio --no-input
                        --log "${WORK}/warmup.log" RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
set(extra "")
if(STALL_MS)
    set(extra --stall-ms ${STALL_MS})
endif()
execute_process(COMMAND "${PLAYER}" "${PROJECT}" --scene "${SCENE}" --frames ${frames} --no-audio --no-input ${extra}
                        --report "${WORK}/report.json" --log "${WORK}/player.log"
                RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
if(NOT code EQUAL 0 OR NOT EXISTS "${WORK}/report.json")
    message(FATAL_ERROR "noor_player failed on ${NAME} (${code}); see ${WORK}/player.log")
endif()
file(READ "${WORK}/report.json" r)
string(JSON errors GET "${r}" errors)
if(NOT errors EQUAL 0)
    message(FATAL_ERROR "${NAME} logged errors: see ${WORK}/player.log")
endif()
# measured value: its path into the report (/ between levels)
set(paths
    cpu_median_ms timing_ms/cpu/median
    cpu_p95_ms timing_ms/cpu/p95
    gpu_median_ms timing_ms/gpu/median
    working_set_mb process/working_set_mb
    private_mb process/private_mb
    gpu_resident_mb process/gpu_resident_mb
    threads process/threads
    first_frame_s process/first_frame_s)
set(failed "")
list(LENGTH paths n)
math(EXPR last "${n} - 1")
foreach(i RANGE 0 ${last} 2)
    math(EXPR j "${i} + 1")
    list(GET paths ${i} key)
    list(GET paths ${j} path)
    string(REPLACE "/" ";" path "${path}")
    string(JSON value GET "${r}" ${path})
    string(JSON base GET "${entry}" measured ${key})
    string(JSON limit GET "${entry}" limits ${key})
    set(verdict "ok")
    if(value GREATER limit)
        set(verdict "OVER")
        list(APPEND failed "${key}")
    endif()
    message(STATUS "${NAME}: ${key} ${value} (baseline ${base}, limit ${limit}) ${verdict}")
endforeach()
if(failed)
    message(FATAL_ERROR "${NAME} costs more than its yardstick allows: ${failed}")
endif()
