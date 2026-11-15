include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)

# ---- Unit tests (no GPU, no window) ----------------------------------------
add_executable(noor_unit_tests
    unit/main.cpp
    unit/test_log.cpp
    unit/test_error.cpp
    unit/test_assert.cpp
    unit/test_guid.cpp
    unit/test_math.cpp
)
target_link_libraries(noor_unit_tests PRIVATE noor::foundation doctest::doctest)
noor_set_target_options(noor_unit_tests)
set_target_properties(noor_unit_tests PROPERTIES FOLDER "Tests")
doctest_discover_tests(noor_unit_tests PROPERTIES LABELS "unit")

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
