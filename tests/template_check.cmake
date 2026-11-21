# Project templates (2D part 7): noor_cli lists NoorEngine's templates, makes a project from one, and that project plays
# (its default scene, a second, under Vulkan validation) with no errors logged.
#   -DCLI=<noor_cli> -DPLAYER=<noor_player> -DTEMPLATE=<id> -DWORK=<folder>
file(REMOVE_RECURSE "${WORK}")
execute_process(COMMAND "${CLI}" templates RESULT_VARIABLE code OUTPUT_VARIABLE listed ERROR_QUIET)
string(FIND "${listed}" "${TEMPLATE}" at)
if(NOT code EQUAL 0 OR at EQUAL -1)
    message(FATAL_ERROR "noor_cli templates doesn't list ${TEMPLATE}: ${listed}")
endif()
execute_process(COMMAND "${CLI}" new "${WORK}/Made" "Made" --template ${TEMPLATE} RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "noor_cli new --template ${TEMPLATE} failed: ${out}${err}")
endif()
execute_process(COMMAND "${PLAYER}" "${WORK}/Made" --frames 60 --no-audio --no-input --validation --windowed
                        --report "${WORK}/report.json" --log "${WORK}/player.log"
                RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
if(NOT code EQUAL 0 OR NOT EXISTS "${WORK}/report.json")
    message(FATAL_ERROR "the project made from ${TEMPLATE} doesn't play (${code}); see ${WORK}/player.log")
endif()
file(READ "${WORK}/report.json" report)
string(JSON errors GET "${report}" errors)
string(JSON hud GET "${report}" hud_text)
message(STATUS "${TEMPLATE}: HUD ${hud}")
if(NOT errors EQUAL 0)
    message(FATAL_ERROR "${errors} error(s) logged playing the project made from ${TEMPLATE}; see ${WORK}/player.log")
endif()
