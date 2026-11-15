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

# --- Vulkan Memory Allocator (MIT, header-only) ------------------------------
FetchContent_Declare(VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        0f1a757da579831283413295cc7f3b47eb521c77 # v3.5.0
    SYSTEM)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

# --- nlohmann/json (MIT): projects, scenes, metas (ADR-005) ------------------
FetchContent_Declare(nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# --- cgltf (MIT, single header): glTF/GLB loading ------------------------------
FetchContent_Declare(cgltf
    URL      https://raw.githubusercontent.com/jkuhlmann/cgltf/360db1a95480fe102ae9c69b27c5d101167ff5ba/cgltf.h
    URL_HASH SHA256=e378a21c084bf1f288bb799de827bb26906efb024255f1ecf1705ea13f11c6ec
    DOWNLOAD_NO_EXTRACT TRUE
    DOWNLOAD_NAME cgltf.h)
FetchContent_MakeAvailable(cgltf)
add_library(noor_third_party_cgltf INTERFACE)
target_include_directories(noor_third_party_cgltf SYSTEM INTERFACE ${cgltf_SOURCE_DIR})
add_library(cgltf::cgltf ALIAS noor_third_party_cgltf)

# --- doctest (MIT, tests only) ---------------------------------------------
if(NOOR_BUILD_TESTS)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        2d0a9359a60c51affe2a9bebb1be1dca47868151 # v2.5.3
        SYSTEM)
    FetchContent_MakeAvailable(doctest)
endif()
