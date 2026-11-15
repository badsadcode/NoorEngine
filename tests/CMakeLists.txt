include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)

# ---- Unit tests (no GPU, no window) ----------------------------------------
add_executable(noor_unit_tests
    unit/main.cpp
    unit/test_log.cpp
    unit/test_error.cpp
    unit/test_assert.cpp
    unit/test_guid.cpp
)
target_link_libraries(noor_unit_tests PRIVATE noor::foundation doctest::doctest)
noor_set_target_options(noor_unit_tests)
set_target_properties(noor_unit_tests PROPERTIES FOLDER "Tests")
doctest_discover_tests(noor_unit_tests PROPERTIES LABELS "unit")

# ---- GPU smoke test ----------------------------------------------------------
# Opens a real window and runs 180 frames with validation layers on.
# Fails on any error, including Vulkan validation errors. Needs a Vulkan 1.3 GPU.
add_test(NAME smoke.noor_diag
    COMMAND noor_diag --frames 180 --log ${CMAKE_CURRENT_BINARY_DIR}/smoke_noor_diag.log)
set_tests_properties(smoke.noor_diag PROPERTIES LABELS "gpu;smoke" TIMEOUT 60)
