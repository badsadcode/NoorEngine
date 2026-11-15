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
)
target_link_libraries(noor_unit_tests PRIVATE noor::foundation noor::asset noor::scene noor::runtime noor::physics noor::scripting doctest::doctest)
target_compile_definitions(noor_unit_tests PRIVATE NOOR_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
# Header-only pieces of render (fly_camera.h) are tested without touching the GPU.
target_include_directories(noor_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/engine/render/include)
noor_set_target_options(noor_unit_tests)
set_target_properties(noor_unit_tests PROPERTIES FOLDER "Tests")
doctest_discover_tests(noor_unit_tests PROPERTIES LABELS "unit")

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
    doctest_discover_tests(noor_editor_api_tests TEST_PREFIX "api: " PROPERTIES LABELS "api")
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
