cmake_minimum_required(VERSION 3.28)

project(NoorEngine
    VERSION 0.0.1
    DESCRIPTION "NoorEngine by Bad Ideas Works"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(NOOR_BUILD_TESTS "Build NoorEngine tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(NoorCompilerOptions)
include(NoorDependencies)
include(NoorShaders)

add_subdirectory(engine/foundation)
add_subdirectory(engine/platform)
add_subdirectory(engine/render)
add_subdirectory(engine/asset)
add_subdirectory(engine/scene)
add_subdirectory(engine/runtime)
add_subdirectory(engine/scripting)
add_subdirectory(tools/noor_cli)
if(WIN32)
    add_subdirectory(engine/editor_api)  # native child-window viewports are Windows-only for now
endif()
add_subdirectory(tools/noor_diag)

if(NOOR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
