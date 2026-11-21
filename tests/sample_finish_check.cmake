# A sample plays itself to the end (2D part 7): NoorPlatformer's autopilot runs the level for fifteen seconds and must
# reach the door ("You made it!" on its HUD), with no errors logged, under Vulkan validation. Proves the level can be
# finished (every ledge reachable, every pit escapable) and keeps it so.
#   -DPLAYER=<noor_player> -DPROJECT=<samples/NoorPlatformer> -DSCENE=<scene> -DFRAMES=<n> -DEXPECT=<HUD text> -DWORK=<folder>
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${PLAYER}" "${PROJECT}" --scene "${SCENE}" --frames ${FRAMES} --no-audio --no-input --validation --windowed
                        --report "${WORK}/report.json" --log "${WORK}/player.log"
                RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
if(NOT code EQUAL 0 OR NOT EXISTS "${WORK}/report.json")
    message(FATAL_ERROR "noor_player failed (${code}); see ${WORK}/player.log")
endif()
file(READ "${WORK}/report.json" report)
string(JSON errors GET "${report}" errors)
string(JSON hud GET "${report}" hud_text)
message(STATUS "HUD: ${hud}")
if(NOT errors EQUAL 0)
    message(FATAL_ERROR "${errors} error(s) logged; see ${WORK}/player.log")
endif()
string(FIND "${hud}" "${EXPECT}" at)
if(at EQUAL -1)
    message(FATAL_ERROR "the HUD never showed '${EXPECT}' (the autopilot didn't get there): ${hud}")
endif()
