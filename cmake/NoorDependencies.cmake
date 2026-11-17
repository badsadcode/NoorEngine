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

# --- Luau (MIT): Lua scripting (ADR-009) -----------------------------------------
set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_WEB OFF CACHE BOOL "" FORCE)
FetchContent_Declare(luau
    GIT_REPOSITORY https://github.com/luau-lang/luau.git
    GIT_TAG        2a3d8937c19c8fd9f7b72b6ec9068c62760e6588 # 0.745
    SYSTEM)
FetchContent_MakeAvailable(luau)

# --- Jolt Physics (MIT): rigid bodies, colliders, character controller (ADR-010) ----------
# Portable CPU baseline (SSE4.2, no AVX/AVX2/FMA) so builds run on older x64 machines; Jolt's GPU
# compute, debug renderer, profiler and object streams are off; same MSVC runtime as the engine.
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(USE_AVX OFF CACHE BOOL "" FORCE)
set(USE_AVX2 OFF CACHE BOOL "" FORCE)
set(USE_LZCNT OFF CACHE BOOL "" FORCE)
set(USE_TZCNT OFF CACHE BOOL "" FORCE)
set(USE_F16C OFF CACHE BOOL "" FORCE)
set(USE_FMADD OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
FetchContent_Declare(JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        e77f175595e64cb44218cc9d9d56fc365ad0e36a # v5.6.0
    SOURCE_SUBDIR  Build
    SYSTEM)
FetchContent_MakeAvailable(JoltPhysics)

# --- miniaudio (public domain / MIT-0, single file): audio (ADR-011) ----------------
FetchContent_Declare(miniaudio
    URL      https://raw.githubusercontent.com/mackron/miniaudio/877ea4a7c2601c7b0f2a44e83dfac42e6470fea6/miniaudio.h
    URL_HASH SHA256=6b376c7fbb1352ba1e0e432ab66069b335ea9405e45cb6c30b97ef0f3c9c3e0e
    DOWNLOAD_NO_EXTRACT TRUE
    DOWNLOAD_NAME miniaudio.h)
FetchContent_MakeAvailable(miniaudio)
# The implementation is compiled once, here, without the engine's warnings-as-errors flags.
add_library(noor_third_party_miniaudio STATIC ${CMAKE_CURRENT_LIST_DIR}/../third_party/miniaudio/miniaudio_impl.c)
target_include_directories(noor_third_party_miniaudio SYSTEM PUBLIC ${miniaudio_SOURCE_DIR})
add_library(miniaudio::miniaudio ALIAS noor_third_party_miniaudio)

# --- stb_truetype (public domain / MIT, single file): HUD text (engine/game) -------------
FetchContent_Declare(stb_truetype
    URL      https://raw.githubusercontent.com/nothings/stb/2a34860c372bc520e1521c6aa7e3e4b4138c76f8/stb_truetype.h
    URL_HASH SHA256=2bd60d66b2af1e54b9ce6dc32393de79e66294137b94b44c92d0237099d3f4f3
    DOWNLOAD_NO_EXTRACT TRUE
    DOWNLOAD_NAME stb_truetype.h)
FetchContent_MakeAvailable(stb_truetype)
add_library(noor_third_party_stb_truetype INTERFACE)
target_include_directories(noor_third_party_stb_truetype SYSTEM INTERFACE ${stb_truetype_SOURCE_DIR})
add_library(stb::truetype ALIAS noor_third_party_stb_truetype)

# --- stb_image / stb_image_write (public domain / MIT, single files): image decoding (textures,
# ADR-029) and the render tests' image comparison (PNG references) ----------------------------
FetchContent_Declare(stb_image
    URL      https://raw.githubusercontent.com/nothings/stb/2a34860c372bc520e1521c6aa7e3e4b4138c76f8/stb_image.h
    URL_HASH SHA256=594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3
    DOWNLOAD_NO_EXTRACT TRUE
    DOWNLOAD_NAME stb_image.h)
FetchContent_MakeAvailable(stb_image)
FetchContent_Declare(stb_image_write
    URL      https://raw.githubusercontent.com/nothings/stb/2a34860c372bc520e1521c6aa7e3e4b4138c76f8/stb_image_write.h
    URL_HASH SHA256=cbd5f0ad7a9cf4468affb36354a1d2338034f2c12473cf1a8e32053cb6914a05
    DOWNLOAD_NO_EXTRACT TRUE
    DOWNLOAD_NAME stb_image_write.h)
FetchContent_MakeAvailable(stb_image_write)
add_library(noor_third_party_stb_image INTERFACE)
target_include_directories(noor_third_party_stb_image SYSTEM INTERFACE ${stb_image_SOURCE_DIR} ${stb_image_write_SOURCE_DIR})
add_library(stb::image ALIAS noor_third_party_stb_image)

# --- Manifold (Apache-2.0): robust mesh booleans for CSG brushes (ADR-016, Milestone 6).
# Editor and cooking only (noor_csg); games load cooked meshes. 3D only: no CrossSection (so no
# Clipper2), no TBB, no tests, bindings or downloads; a static library.
set(MANIFOLD_CROSS_SECTION OFF CACHE BOOL "" FORCE)
set(MANIFOLD_PAR OFF CACHE BOOL "" FORCE)
set(MANIFOLD_TEST OFF CACHE BOOL "" FORCE)
set(MANIFOLD_PYBIND OFF CACHE BOOL "" FORCE)
set(MANIFOLD_CBIND OFF CACHE BOOL "" FORCE)
set(MANIFOLD_DOWNLOADS OFF CACHE BOOL "" FORCE)
set(MANIFOLD_DEBUG OFF CACHE BOOL "" FORCE)
set(MANIFOLD_STRICT OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS_BEFORE_MANIFOLD ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(manifold
    GIT_REPOSITORY https://github.com/elalish/manifold.git
    GIT_TAG        ce50d78021d64507f89e8c9fc2c2e51018117857 # v3.5.4
    SYSTEM)
FetchContent_MakeAvailable(manifold)
set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_BEFORE_MANIFOLD})

# --- bc7enc + rgbcx (Richard Geldreich; MIT or public domain, dual): BC7 and BC5 texture
# compression on the CPU (ADR-029, Milestone 5). Only the plain C++ files, not bc7e.ispc (Apache).
# Each file is pinned to a commit and its SHA-256, and compiled once without the engine's
# warnings-as-errors flags.
set(NOOR_BC7ENC_COMMIT b9438627eef73a1157e84201b6fa6eb2ffd6d9f0)
set(NOOR_BC7ENC_DIR ${CMAKE_BINARY_DIR}/_deps/bc7enc-${NOOR_BC7ENC_COMMIT})
set(NOOR_BC7ENC_FILES
    "LICENSE=b3e843763e8d3bdeb0f469ecd4107fc32b5fa27ebee1af10d2066bc339292800"
    "bc7enc.cpp=b90444a091530a13c61fa6375583fcc9bc9ca47e474fd71f818876de68c1b581"
    "bc7enc.h=f40f4a956ed6a8845b7909e83831de09512424408b32ca29551bf49aee2fecf6"
    "bc7decomp.cpp=cfbe156926ebd96443b4407a9da1b5b7a424fdf989fedca3cf2fd0515e0c368a"
    "bc7decomp.h=cbba8778640e0021f55ca686b6bc19e6960032539084c70af698922b05c5b242"
    "rgbcx.cpp=059243ce8e8e3ec634bc1e3da3996115aaf36196a8d75a24d3dbb3c8bfe0ca98"
    "rgbcx.h=111bd4c0211eca172bec9d757e2e597a47c4e4731bb122c9e8761dca85711bba"
    "rgbcx_table4_small.h=d484802656fb7f2249674263bbb14399ab9ccb2a997ac0030e51b17a4bf8595f")
foreach(entry IN LISTS NOOR_BC7ENC_FILES)
    string(REPLACE "=" ";" parts "${entry}")
    list(GET parts 0 name)
    list(GET parts 1 hash)
    set(target_file ${NOOR_BC7ENC_DIR}/${name})
    if(EXISTS ${target_file})
        file(SHA256 ${target_file} have)
    else()
        set(have "")
    endif()
    if(NOT have STREQUAL hash)
        file(DOWNLOAD https://raw.githubusercontent.com/richgel999/bc7enc_rdo/${NOOR_BC7ENC_COMMIT}/${name} ${target_file}
             EXPECTED_HASH SHA256=${hash} STATUS dl_status)
        list(GET dl_status 0 dl_code)
        if(NOT dl_code EQUAL 0)
            message(FATAL_ERROR "bc7enc: couldn't download ${name}: ${dl_status}")
        endif()
    endif()
endforeach()
add_library(noor_third_party_bc7enc STATIC ${NOOR_BC7ENC_DIR}/bc7enc.cpp ${NOOR_BC7ENC_DIR}/bc7decomp.cpp ${NOOR_BC7ENC_DIR}/rgbcx.cpp)
target_include_directories(noor_third_party_bc7enc SYSTEM PUBLIC ${NOOR_BC7ENC_DIR})
target_compile_definitions(noor_third_party_bc7enc PUBLIC RGBCX_USE_SMALLER_TABLES=1)
if(MSVC)
    target_compile_options(noor_third_party_bc7enc PRIVATE /W0)
else()
    target_compile_options(noor_third_party_bc7enc PRIVATE -w)
endif()
add_library(bc7enc::bc7enc ALIAS noor_third_party_bc7enc)

# --- Atkinson Hyperlegible (SIL OFL 1.1): the default HUD font, embedded in the engine ------
FetchContent_Declare(hud_font
    URL      https://raw.githubusercontent.com/google/fonts/1b22086d13bdbbf5ec63d01a39a49246e4ac355f/ofl/atkinsonhyperlegible/AtkinsonHyperlegible-Regular.ttf
    URL_HASH SHA256=7fb917c89019896d0b52ee84b7cbb3304c18cb90b19a62f5e32712bd23e97669
    DOWNLOAD_NO_EXTRACT TRUE
    DOWNLOAD_NAME AtkinsonHyperlegible-Regular.ttf)
FetchContent_MakeAvailable(hud_font)
set(NOOR_HUD_FONT ${hud_font_SOURCE_DIR}/AtkinsonHyperlegible-Regular.ttf CACHE INTERNAL "The HUD font (OFL)")

# --- doctest (MIT, tests only) ---------------------------------------------
if(NOOR_BUILD_TESTS)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        2d0a9359a60c51affe2a9bebb1be1dca47868151 # v2.5.3
        SYSTEM)
    FetchContent_MakeAvailable(doctest)
endif()
