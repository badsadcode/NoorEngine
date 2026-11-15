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
)
target_link_libraries(noor_unit_tests PRIVATE noor::foundation noor::asset noor::scene doctest::doctest)
target_compile_definitions(noor_unit_tests PRIVATE NOOR_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
# Header-only pieces of render (fly_camera.h) are tested without touching the GPU.
target_include_directories(noor_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/engine/render/include)
noor_set_target_options(noor_unit_tests)
set_target_properties(noor_unit_tests PROPERTIES FOLDER "Tests")
doctest_discover_tests(noor_unit_tests PROPERTIES LABELS "unit")

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
