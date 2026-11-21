# Display settings on screen (2D foundations, part 6): a 2D scene of tests/data/render_scenes played with display
# settings given by --display, at a chosen window size, then checked:
#   viewport, keep   320 x 180 at 1280 x 720: each game pixel exactly 4 x 4 screen pixels, no bars
#   viewport, keep   320 x 180 at 1280 x 800: still 4x, black bars of 40 above and below
#   viewport, expand 320 x 180 at 1280 x 800: 320 x 200 game pixels, 4x, no bars, keep's picture in the middle
#   canvas, render scale 0.5 at 1280 x 720: the scene drawn at 640 x 360, the picture the whole window
# Each with Vulkan validation and no errors logged.
#   -DPLAYER=<noor_player> -DCHECK=<noor_display_check> -DPROJECT=<folder> -DWORK=<folder>
file(MAKE_DIRECTORY "${WORK}")

function(play name window settings)
    set(dir "${WORK}/${name}")
    file(MAKE_DIRECTORY "${dir}")
    file(WRITE "${dir}/display.json" "${settings}")
    file(REMOVE "${dir}/shot.bmp")
    execute_process(COMMAND "${PLAYER}" "${PROJECT}" --scene Scenes/Tilemap_Square.noorscene --frames 5 --no-audio --no-input
                            --validation --windowed --window ${window} --display "${dir}/display.json"
                            --screenshot "${dir}/shot.bmp" --report "${dir}/report.json" --log "${dir}/player.log"
                    RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
    if(NOT code EQUAL 0 OR NOT EXISTS "${dir}/shot.bmp")
        message(FATAL_ERROR "${name}: noor_player failed (${code}); see ${dir}/player.log")
    endif()
    file(READ "${dir}/report.json" report)
    string(JSON errors GET "${report}" errors)
    if(NOT errors EQUAL 0)
        message(FATAL_ERROR "${name}: ${errors} error(s) logged; see ${dir}/player.log")
    endif()
    string(JSON d GET "${report}" display)
    set(REPORT_DISPLAY "${d}" PARENT_SCOPE)
    message(STATUS "${name}: ${d}")
endfunction()

function(expect name key json)
    string(JSON actual GET "${REPORT_DISPLAY}" ${key})
    string(JSON same EQUAL "${actual}" "${json}")
    if(NOT same)
        message(FATAL_ERROR "${name}: display ${key} is ${actual}, expected ${json}")
    endif()
endfunction()

function(blocks name block)
    string(JSON x GET "${REPORT_DISPLAY}" rect 0)
    string(JSON y GET "${REPORT_DISPLAY}" rect 1)
    string(JSON w GET "${REPORT_DISPLAY}" rect 2)
    string(JSON h GET "${REPORT_DISPLAY}" rect 3)
    execute_process(COMMAND "${CHECK}" "${WORK}/${name}/shot.bmp" ${x} ${y} ${w} ${h} ${block}
                    RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
    message(STATUS "${name}: ${out}${err}")
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "${name}: the picture isn't whole ${block} x ${block} game pixels with black bars: see ${WORK}/${name}/shot.bmp")
    endif()
endfunction()

play(keep_720 1280x720 [=[{"base_resolution": [320, 180], "stretch": "viewport"}]=])
expect(keep_720 screen "[1280, 720]")
expect(keep_720 render "[320, 180]")
expect(keep_720 rect "[0, 0, 1280, 720]")
blocks(keep_720 4)

play(keep_800 1280x800 [=[{"base_resolution": [320, 180], "stretch": "viewport"}]=])
expect(keep_800 render "[320, 180]")
expect(keep_800 rect "[0, 40, 1280, 720]")
blocks(keep_800 4)

play(expand_800 1280x800 [=[{"base_resolution": [320, 180], "stretch": "viewport", "aspect": "expand"}]=])
expect(expand_800 render "[320, 200]")
expect(expand_800 rect "[0, 0, 1280, 800]")
blocks(expand_800 4)
# The same world as keep_720, with 10 more game pixels (40 screen pixels) above and below: not squashed.
execute_process(COMMAND "${CHECK}" --same "${WORK}/keep_720/shot.bmp" 0 0 "${WORK}/expand_800/shot.bmp" 0 40 1280 720
                RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
message(STATUS "expand_800 against keep_720: ${out}${err}")
if(NOT code EQUAL 0)
    message(FATAL_ERROR "expand doesn't show keep's picture in its middle: compare ${WORK}/keep_720/shot.bmp and ${WORK}/expand_800/shot.bmp")
endif()

play(canvas_half 1280x720 [=[{"base_resolution": [1280, 720], "stretch": "canvas", "render_scale": 0.5}]=])
expect(canvas_half render "[640, 360]")
expect(canvas_half rect "[0, 0, 1280, 720]")
