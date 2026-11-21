# Gamepads and rebinding (gameplay foundations, part 6e), with a simulated pad (noor_player --pad-script):
#   - tests/data/ui_project's Pad scene: the pad moves a menu's focus (d-pad, then stick), presses its buttons (South, which
#     also jumps), is unplugged and plugged back in as player 1 again, and a second pad jumps as player 2. The Lua scene
#     in the project, and the Lua and C# scenes in a built game, must show exactly the same HUD.
#   - its Rebind scene, twice with the same player's settings folder: the first run makes J jump and saves it, the second
#     finds it (Lua in the project, C# in the built game, each with a folder of its own).
#   - NoorPlatformer: the pad runs the hero left (the sample's autopilot only ever runs right) and jumps.
#   -DPLAYER=<noor_player> -DGAME=<the built ui_project's .exe> -DPROJECT=<tests/data/ui_project>
#   -DPLATFORMER=<samples/NoorPlatformer> -DWORK=<folder>
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(args --no-audio --no-input --windowed)

function(play name exe)
    execute_process(COMMAND "${exe}" ${ARGN} ${args} --report "${WORK}/${name}.json" --log "${WORK}/${name}.log"
                    RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
    if(NOT code EQUAL 0 OR NOT EXISTS "${WORK}/${name}.json")
        message(FATAL_ERROR "${name}: the player failed (${code}); see ${WORK}/${name}.log")
    endif()
    file(READ "${WORK}/${name}.json" report)
    string(JSON errors GET "${report}" errors)
    if(NOT errors EQUAL 0)
        message(FATAL_ERROR "${name}: ${errors} error(s) logged; see ${WORK}/${name}.log")
    endif()
    set(report "${report}" PARENT_SCOPE)
endfunction()

function(expect_hud name expect)
    string(JSON line GET "${report}" hud_text 0)
    message(STATUS "${name}: '${line}'")
    if(NOT line STREQUAL expect)
        message(FATAL_ERROR "${name}: expected '${expect}'")
    endif()
endfunction()

# The menu.
set(menu "connected 1 Test pad; click options; jump 1; gone 1; connected 1 Test pad; click quit; jump 1; connected 2 Second pad; jump 2")
set(pad --frames 100 --pad-script "${PROJECT}/pad_script.json")
play(menu_project_lua "${PLAYER}" "${PROJECT}" --scene Scenes/Pad.noorscene ${pad})
expect_hud(menu_project_lua "${menu}")
play(menu_game_lua "${GAME}" --scene Scenes/Pad.noorscene ${pad})
expect_hud(menu_game_lua "${menu}")
play(menu_game_csharp "${GAME}" --scene Scenes/Pad_CSharp.noorscene ${pad})
expect_hud(menu_game_csharp "${menu}")

# Rebinding across a restart.
foreach(variant IN ITEMS lua csharp)
    if(variant STREQUAL "lua")
        set(exe "${PLAYER}")
        set(where "${PROJECT}" --scene Scenes/Rebind.noorscene)
    else()
        set(exe "${GAME}")
        set(where --scene Scenes/Rebind_CSharp.noorscene)
    endif()
    play(rebind_${variant}_1 "${exe}" ${where} --frames 5 --user-dir "${WORK}/user_${variant}")
    expect_hud(rebind_${variant}_1 "saved: J,Pad.South")
    play(rebind_${variant}_2 "${exe}" ${where} --frames 5 --user-dir "${WORK}/user_${variant}")
    expect_hud(rebind_${variant}_2 "remembered: J,Pad.South")
endforeach()

# The platformer: left from x 3.0 (where the hero starts) and up from y 1.5 (the ground), 10 frames after South.
file(WRITE "${WORK}/platformer_pad.json" [=[{"events": [
  {"frame": 1, "player": 1, "connect": "Test pad"},
  {"frame": 20, "axes": {"LeftX": -1.0}},
  {"frame": 70, "press": ["Pad.South"]},
  {"frame": 74, "release": ["Pad.South"]}
]}]=])
play(platformer "${PLAYER}" "${PLATFORMER}" --frames 80 --pad-script "${WORK}/platformer_pad.json")
string(JSON x GET "${report}" entities Hero 0)
string(JSON y GET "${report}" entities Hero 1)
message(STATUS "platformer: the hero at ${x}, ${y}")
if(x GREATER_EQUAL 2.5 OR y LESS_EQUAL 2.0)
    message(FATAL_ERROR "platformer: the pad should have run the hero left (x below 2.5) and up (y above 2.0)")
endif()
