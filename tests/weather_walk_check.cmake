# Walking into a weather zone (Milestone 8A, part 7): render_scenes/Weather_Walk's camera walks (Scripts/WalkForward.luau,
# 10 m/s along -Z) from a clear noon into a fog bank (a weather_zone with fog and an overcast deck, blending over 20 m).
# The fog and the exposure are recorded every frame (noor_player --trace-environment), first from the project, then
# from a game built by noor_cli; noor_trace_check judges the walk (smooth, never backwards, the zone fully on at the
# end), that the built game blends exactly like the project, and that frames don't stall gathering the scene (p95 under 8 ms, none over 120)
# (a blend once rebuilt the atmosphere's tables every frame: 20 FPS).
#   -DCLI=<noor_cli> -DPLAYER=<noor_player> -DCHECK=<noor_trace_check> -DPROJECT=<render_scenes> -DWORK=<folder>
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
file(COPY "${PROJECT}/" DESTINATION "${WORK}/project")
file(REMOVE_RECURSE "${WORK}/project/.noor")
set(args --scene Scenes/Weather_Walk.noorscene --frames 540 --no-audio --no-input --trace-environment)
execute_process(COMMAND "${PLAYER}" "${WORK}/project" ${args} --report "${WORK}/project.json" --log "${WORK}/project.log"
                RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
if(NOT code EQUAL 0 OR NOT EXISTS "${WORK}/project.json")
    message(FATAL_ERROR "noor_player failed on the project (${code}); see ${WORK}/project.log")
endif()
execute_process(COMMAND "${CLI}" build "${WORK}/project" "${WORK}/game" RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "noor_cli build failed (${code}): ${out}${err}")
endif()
file(GLOB exe "${WORK}/game/*.exe")
execute_process(COMMAND "${exe}" ${args} --report "${WORK}/game.json" --log "${WORK}/game.log" RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
if(NOT code EQUAL 0 OR NOT EXISTS "${WORK}/game.json")
    message(FATAL_ERROR "the built game failed (${code}); see ${WORK}/game.log")
endif()
execute_process(COMMAND "${CHECK}" "${WORK}/project.json" "${WORK}/game.json" --fog-from 0 --fog-to 0.03 --collect-max-ms 8
                RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
message(STATUS "${out}${err}")
if(NOT code EQUAL 0)
    message(FATAL_ERROR "the walk into the fog bank isn't right")
endif()
