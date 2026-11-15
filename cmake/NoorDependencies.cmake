# Third-party dependencies. Every entry here must also be listed in
# third_party/manifest.json (policy: docs/decisions/ADR-021).

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --- Vulkan (installed SDK; loader comes with the GPU driver) -------------
find_package(Vulkan 1.3 REQUIRED)

# --- SDL3 (zlib) -----------------------------------------------------------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        829a65d769d935c4852f8159e964312c0957260a # release-3.4.18
    GIT_SHALLOW    FALSE
    SYSTEM)
FetchContent_MakeAvailable(SDL3)

# --- doctest (MIT, tests only) ---------------------------------------------
if(NOOR_BUILD_TESTS)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        2d0a9359a60c51affe2a9bebb1be1dca47868151 # v2.5.3
        SYSTEM)
    FetchContent_MakeAvailable(doctest)
endif()
