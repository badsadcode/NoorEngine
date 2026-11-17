include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)

# ---- Unit tests (no GPU, no window) ----------------------------------------
add_executable(noor_unit_tests
    unit/main.cpp
    unit/test_log.cpp
    unit/test_error.cpp
    unit/test_assert.cpp
    unit/test_guid.cpp
    unit/test_math.cpp
    unit/test_fly_camera.cpp
    unit/test_files_hash.cpp
    unit/test_assets_scenes.cpp
    unit/test_history_migration_reload.cpp
    unit/test_prefabs.cpp
    unit/test_runtime.cpp
    unit/test_lua_scripts.cpp
    unit/test_physics.cpp
    unit/test_character.cpp
    unit/test_csharp_host.cpp
    unit/test_audio.cpp
    unit/test_native_window.cpp
    unit/test_hud.cpp
    unit/test_texture.cpp
)
target_link_libraries(noor_unit_tests PRIVATE noor::foundation noor::platform noor::asset noor::scene noor::runtime noor::physics noor::audio noor::scripting noor::game doctest::doctest stb::image)
target_compile_definitions(noor_unit_tests PRIVATE NOOR_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
# Header-only pieces of render (fly_camera.h) are tested without touching the GPU.
target_include_directories(noor_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/engine/render/include)
noor_set_target_options(noor_unit_tests)
set_target_properties(noor_unit_tests PROPERTIES FOLDER "Tests")
# A ctest entry that matched no test case must fail, not pass. (A ';' in a TEST_CASE name splits
# it into two entries that match nothing: CMake lists use ';'. That hid 15 tests until 2026-11-16.)
set(NOOR_NOTHING_RAN "test cases: +0 ")  # no pipe character here: it breaks cmd quoting in the discovery step
doctest_discover_tests(noor_unit_tests PROPERTIES LABELS "unit" FAIL_REGULAR_EXPRESSION "${NOOR_NOTHING_RAN}")

# ---- Editor API tests: the document C ABI, as NoorEditor uses it (headless) ----
if(WIN32)
    # The viewport maths is internal to the DLL, so its source is compiled into the test too.
    add_executable(noor_editor_api_tests api/test_document_api.cpp api/test_viewport_tools.cpp
                   ${PROJECT_SOURCE_DIR}/engine/editor_api/src/viewport_tools.cpp)
    target_include_directories(noor_editor_api_tests PRIVATE ${PROJECT_SOURCE_DIR}/engine/editor_api/src)
    target_link_libraries(noor_editor_api_tests PRIVATE noor_editor_api noor::scene doctest::doctest)
    target_compile_definitions(noor_editor_api_tests PRIVATE NOOR_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    noor_set_target_options(noor_editor_api_tests)
    set_target_properties(noor_editor_api_tests PROPERTIES FOLDER "Tests")
    add_custom_command(TARGET noor_editor_api_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:noor_editor_api> $<TARGET_FILE_DIR:noor_editor_api_tests>)
    doctest_discover_tests(noor_editor_api_tests TEST_PREFIX "api: " PROPERTIES LABELS "api" FAIL_REGULAR_EXPRESSION "${NOOR_NOTHING_RAN}")
endif()

# ---- CLI tests (headless, run in CI) ---------------------------------------
# Full project workflow through noor_cli: new -> copy building -> scan -> add-model -> validate.
add_test(NAME cli.project_workflow
    COMMAND ${CMAKE_COMMAND}
        -DCLI=$<TARGET_FILE:noor_cli>
        -DDATA=${CMAKE_CURRENT_SOURCE_DIR}/data
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli_workflow
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_workflow.cmake)
set_tests_properties(cli.project_workflow PROPERTIES LABELS "cli" TIMEOUT 60)

# noor_cli build packs the test project with the player (headless: only copies and checks files).
add_test(NAME cli.build
    COMMAND ${CMAKE_COMMAND}
        -DCLI=$<TARGET_FILE:noor_cli>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/player_project
        -DOUT=${CMAKE_CURRENT_BINARY_DIR}/built_game
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_build.cmake)
set_tests_properties(cli.build PROPERTIES LABELS "cli" TIMEOUT 60 FIXTURES_SETUP built_game)

# ---- GPU tests ---------------------------------------------------------------
# Need a Vulkan 1.3 GPU and a desktop. Validation layers on; any validation error fails.

# Runs 180 frames of the spinning cube.
add_test(NAME smoke.noor_diag
    COMMAND noor_diag --frames 180 --log ${CMAKE_CURRENT_BINARY_DIR}/smoke_noor_diag.log)
set_tests_properties(smoke.noor_diag PROPERTIES LABELS "gpu;smoke" TIMEOUT 60)

# Reads a rendered frame back from the GPU and checks real pixels (background color,
# cube front and top faces): catches projection, culling, depth and color-space bugs.
add_test(NAME render.cube_pixels
    COMMAND noor_diag --frames 60 --verify-render --log ${CMAKE_CURRENT_BINARY_DIR}/render_cube_pixels.log)
set_tests_properties(render.cube_pixels PROPERTIES LABELS "gpu;render" TIMEOUT 60)

# 2,500 spinning cubes + ground (2,502 draw calls per frame) for 120 frames with validation on:
# catches leaks, sync errors and slow paths under load. Frame timings go to the log.
add_test(NAME stress.cubes_2500
    COMMAND noor_diag --frames 120 --cubes 2500 --log ${CMAKE_CURRENT_BINARY_DIR}/stress_cubes_2500.log)
set_tests_properties(stress.cubes_2500 PROPERTIES LABELS "gpu;stress" TIMEOUT 120)

# A real BadBuilding block (Noor Interchange v0.2) imported and rendered with validation on.
add_test(NAME render.glb_building
    COMMAND noor_diag --frames 60 --glb ${CMAKE_CURRENT_SOURCE_DIR}/data/badbuilding/osm_w117821721.glb
            --log ${CMAKE_CURRENT_BINARY_DIR}/render_glb_building.log)
set_tests_properties(render.glb_building PROPERTIES LABELS "gpu;render" TIMEOUT 60)

# Window lifecycle: minimize, restore, resize (and a monitor move when 2+ displays exist),
# checking that rendering pauses/resumes and the swapchain follows the window.
add_test(NAME lifecycle.window
    COMMAND noor_diag --lifecycle-test --log ${CMAKE_CURRENT_BINARY_DIR}/lifecycle_window.log)
set_tests_properties(lifecycle.window PROPERTIES LABELS "gpu;lifecycle" TIMEOUT 60)

# NoorEditor end to end: builds the C# editor against this build's noor_editor_api.dll and runs
# its --smoke self-test (project, add model, inspector edits, undo/redo, save, float viewport).
find_program(NOOR_DOTNET dotnet)
if(WIN32 AND NOOR_DOTNET)
    add_test(NAME editor.smoke
        COMMAND ${CMAKE_COMMAND}
            -DDOTNET=${NOOR_DOTNET}
            -DPROJECT=${PROJECT_SOURCE_DIR}/editor/NoorEditor/NoorEditor.csproj
            -DNATIVE_DIR=$<TARGET_FILE_DIR:noor_editor_api>
            -DOUT=${CMAKE_CURRENT_BINARY_DIR}/editor_smoke/$<CONFIG>
            -DDATA=${CMAKE_CURRENT_SOURCE_DIR}/data
            -DCONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/editor_smoke.cmake)
    set_tests_properties(editor.smoke PROPERTIES LABELS "gpu;editor" TIMEOUT 300)
endif()

# Golden-image render tests (Milestone 5, ADR-012): scenes drawn by noor_player, compared with
# reference pictures in tests/golden by noor_image_compare (tests/tools/image_compare.cpp).
add_executable(noor_image_compare tools/image_compare.cpp)
target_link_libraries(noor_image_compare PRIVATE stb::image)
foreach(name IN ITEMS Daylight Sunset Textured)
    add_test(NAME render.golden_${name}
        COMMAND ${CMAKE_COMMAND}
            -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/${name}.noorscene
            -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/${name}.png
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_${name}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
    set_tests_properties(render.golden_${name} PROPERTIES LABELS "gpu;render" TIMEOUT 60 FIXTURES_SETUP golden_${name})
endforeach()
# The judge must notice a different picture: today's daylight against the sunset reference fails.
add_test(NAME render.golden_catches_a_change
    COMMAND noor_image_compare ${CMAKE_CURRENT_BINARY_DIR}/render_golden_Daylight/screenshot.bmp
                               ${CMAKE_CURRENT_SOURCE_DIR}/golden/Sunset.png)
set_tests_properties(render.golden_catches_a_change PROPERTIES LABELS "gpu;render" WILL_FAIL TRUE FIXTURES_REQUIRED golden_Daylight)

# noor_player runs a project, then the game noor_cli build made, with no editor: physics, a Lua
# script and rendering through the scene's camera (tests/player_check.cmake).
add_test(NAME player.project
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/player_project
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/player_project -DHUD_VERTICES=48
        -P ${CMAKE_CURRENT_SOURCE_DIR}/player_check.cmake)
set_tests_properties(player.project PROPERTIES LABELS "gpu;player" TIMEOUT 60)
add_test(NAME player.built_game
    COMMAND ${CMAKE_COMMAND}
        "-DPLAYER=${CMAKE_CURRENT_BINARY_DIR}/built_game/Player Test.exe"
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/player_built_game -DHUD_VERTICES=48
        -P ${CMAKE_CURRENT_SOURCE_DIR}/player_check.cmake)
set_tests_properties(player.built_game PROPERTIES LABELS "gpu;player" TIMEOUT 60 FIXTURES_REQUIRED built_game)

# The same with a C# script: noor_cli build compiles it (needs the .NET SDK), the built game starts
# .NET through hostfxr (needs the .NET 10 runtime) and the script must have run.
if(TARGET noor_scripting_managed)
    add_test(NAME cli.build_csharp
        COMMAND $<TARGET_FILE:noor_cli> build ${CMAKE_CURRENT_SOURCE_DIR}/data/player_project_csharp ${CMAKE_CURRENT_BINARY_DIR}/built_csharp_game)
    set_tests_properties(cli.build_csharp PROPERTIES LABELS "cli" TIMEOUT 180 FIXTURES_SETUP built_csharp_game
                         PASS_REGULAR_EXPRESSION "C# scripts compiled into managed/Scripts.dll")
    add_test(NAME cli.build_csharp_error
        COMMAND ${CMAKE_COMMAND}
            -DCLI=$<TARGET_FILE:noor_cli>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/player_project_csharp
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli_build_csharp_error
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_build_csharp_error.cmake)
    set_tests_properties(cli.build_csharp_error PROPERTIES LABELS "cli" TIMEOUT 180)
    add_test(NAME player.built_csharp_game
        COMMAND ${CMAKE_COMMAND}
            "-DPLAYER=${CMAKE_CURRENT_BINARY_DIR}/built_csharp_game/Player Test CSharp.exe"
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/player_built_csharp_game -DCSHARP=ON -DHUD_VERTICES=60
            -P ${CMAKE_CURRENT_SOURCE_DIR}/player_check.cmake)
    set_tests_properties(player.built_csharp_game PROPERTIES LABELS "gpu;player" TIMEOUT 60 FIXTURES_REQUIRED built_csharp_game)

    # The sample game, NoorFPS-lite (Milestone 4, part 8): built like a user would, then both the
    # C# scene and its Lua twin play their demo and must score.
    add_test(NAME cli.build_sample_fps
        COMMAND $<TARGET_FILE:noor_cli> build ${PROJECT_SOURCE_DIR}/samples/NoorFPS-lite ${CMAKE_CURRENT_BINARY_DIR}/built_sample_fps)
    set_tests_properties(cli.build_sample_fps PROPERTIES LABELS "cli;sample" TIMEOUT 180 FIXTURES_SETUP built_sample_fps
                         PASS_REGULAR_EXPRESSION "Built 'NoorFPS-lite'")
    foreach(variant IN ITEMS csharp lua)
        set(scene Scenes/Main.noorscene)
        if(variant STREQUAL "lua")
            set(scene Scenes/Main_Lua.noorscene)
        endif()
        add_test(NAME sample.fps_${variant}
            COMMAND ${CMAKE_COMMAND}
                "-DPLAYER=${CMAKE_CURRENT_BINARY_DIR}/built_sample_fps/NoorFPS-lite.exe"
                -DSCENE=${scene} -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sample_fps_${variant}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/sample_fps_check.cmake)
        set_tests_properties(sample.fps_${variant} PROPERTIES LABELS "gpu;sample" TIMEOUT 90 FIXTURES_REQUIRED built_sample_fps)
    endforeach()
endif()
