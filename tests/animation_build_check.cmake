# Skeletal animation played by scripts, the same in a built game (Milestone 8C, part 2, ADR-036). A copy of render_scenes
# gets tests/data/animation_scripts (Ward walking, waving when a script says, nodding when the wave has ended and walking
# on when the nod has: in Lua, and in C#). Each runs 240 frames: the Lua scene in the project and in a game built by
# noor_cli, the C# scene in the built game (C# runs in built games). All three must get through both end events at the
# same moments and leave every bone in the same place (the animation fingerprint: the last frame's poses, bit for bit).
# Part 4 runs Ward_Walk (root motion through a character controller, footstep events): where he ends up must match too.
# Part 3 runs Ward_Controller the same way (a script driving Ward.nooranim): -DSCENE=Ward_Controller -DEXPECT_ANIMATION=Move.
#   -DCLI=<noor_cli> -DPLAYER=<noor_player> -DPROJECT=<render_scenes> -DSCRIPTS=<animation_scripts> -DWORK=<folder>
#   [-DSCENE=<name: Scenes/<name>.noorscene and <name>_CSharp>] [-DEXPECT_ANIMATION=<at the end>] [-DEXPECT_ENDED=<count>]
#   [-DSAME_PLACE=ON]
if(NOT SCENE)
    set(SCENE Ward_Script)
endif()
if(NOT EXPECT_ANIMATION)
    set(EXPECT_ANIMATION Walk)
endif()
if(NOT DEFINED EXPECT_ENDED)
    set(EXPECT_ENDED 2)
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
file(COPY "${PROJECT}/" DESTINATION "${WORK}/project")
file(REMOVE_RECURSE "${WORK}/project/.noor")
file(COPY "${SCRIPTS}/" DESTINATION "${WORK}/project")

function(run_player exe scene report)
    execute_process(COMMAND "${exe}" ${ARGN} --scene ${scene} --frames 240 --no-audio --no-input --report "${report}.json" --log "${report}.log"
                    RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
    if(NOT code EQUAL 0 OR NOT EXISTS "${report}.json")
        message(FATAL_ERROR "${exe} failed on ${scene} (${code}); see ${report}.log")
    endif()
endfunction()

# Ward's clip and when it started, how many clips ended, the pose fingerprint, and how many errors were logged.
function(read_report file prefix)
    file(READ "${file}" r)
    string(JSON n LENGTH "${r}" animation entities)
    if(n LESS 1)
        message(FATAL_ERROR "${file}: no animators")
    endif()
    string(JSON anim GET "${r}" animation entities 0 animation)
    string(JSON started GET "${r}" animation entities 0 started)
    string(JSON ended GET "${r}" animation ended)
    string(JSON print GET "${r}" animation fingerprint)
    string(JSON where GET "${r}" animation entities 0 position)
    string(JSON errors GET "${r}" errors)
    foreach(v anim started ended print where errors)
        set(${prefix}_${v} "${${v}}" PARENT_SCOPE)
    endforeach()
endfunction()

run_player("${PLAYER}" Scenes/${SCENE}.noorscene "${WORK}/project" "${WORK}/project")
execute_process(COMMAND "${CLI}" build "${WORK}/project" "${WORK}/game" RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "noor_cli build failed (${code}): ${out}${err}")
endif()
file(GLOB exe "${WORK}/game/*.exe")
run_player("${exe}" Scenes/${SCENE}.noorscene "${WORK}/game")
run_player("${exe}" Scenes/${SCENE}_CSharp.noorscene "${WORK}/game_csharp")

read_report("${WORK}/project.json" p)
read_report("${WORK}/game.json" g)
read_report("${WORK}/game_csharp.json" c)
message(STATUS "project (Lua):    ${p_anim} since ${p_started}, ${p_ended} clips ended, pose ${p_print}, at ${p_where}")
message(STATUS "built game (Lua): ${g_anim} since ${g_started}, ${g_ended} clips ended, pose ${g_print}, at ${g_where}")
message(STATUS "built game (C#):  ${c_anim} since ${c_started}, ${c_ended} clips ended, pose ${c_print}, at ${c_where}")
if(NOT p_errors EQUAL 0 OR NOT g_errors EQUAL 0 OR NOT c_errors EQUAL 0)
    message(FATAL_ERROR "errors were logged: see the .log files in ${WORK}")
endif()
foreach(run IN ITEMS p g c)
    if(NOT ${run}_anim STREQUAL "${EXPECT_ANIMATION}" OR NOT ${run}_ended EQUAL ${EXPECT_ENDED})
        message(FATAL_ERROR "${SCENE}: Ward didn't get to the end (${run}: ${${run}_anim}, ${${run}_ended} ended; "
                            "${EXPECT_ANIMATION} and ${EXPECT_ENDED} expected)")
    endif()
endforeach()
if(NOT p_started STREQUAL g_started OR NOT p_started STREQUAL c_started)  # (null for a controller: its state has no start time)
    message(FATAL_ERROR "the walk restarted at different moments (project ${p_started}, game ${g_started}, C# ${c_started})")
endif()
# Root motion moves characters (part 4): with -DSAME_PLACE=ON (scenes that start them in the same place), where they
# end up must match too.
if(SAME_PLACE AND (NOT p_where STREQUAL g_where OR NOT p_where STREQUAL c_where))
    message(FATAL_ERROR "Ward ended up in different places (project ${p_where}, game ${g_where}, C# ${c_where})")
endif()
if(NOT p_print STREQUAL g_print OR NOT p_print STREQUAL c_print)
    message(FATAL_ERROR "Ward's bones ended up in different places (project ${p_print}, game ${g_print}, C# ${c_print})")
endif()
