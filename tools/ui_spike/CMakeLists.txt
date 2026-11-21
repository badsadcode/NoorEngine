# The UI spike (gameplay foundations, part 6; ADR-041): the same pause menu with our own widgets and with RmlUi, drawn
# into pictures by one CPU rasterizer. Not part of the engine: only with -DNOOR_UI_SPIKE=ON, which fetches RmlUi 6.3 (MIT)
# and FreeType 2.14.3 (the FreeType licence) for it.
include(FetchContent)
FetchContent_Declare(Freetype
    URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-14-3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    OVERRIDE_FIND_PACKAGE)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Freetype)
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()
FetchContent_Declare(RmlUi
    URL https://github.com/mikke89/RmlUi/archive/refs/tags/6.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)
set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)
FetchContent_MakeAvailable(RmlUi)

add_executable(noor_ui_spike main.cpp own_ui.cpp rml_ui.cpp)
target_link_libraries(noor_ui_spike PRIVATE noor::game noor::runtime RmlUi::RmlUi stb::image)
target_compile_features(noor_ui_spike PRIVATE cxx_std_23)
set_target_properties(noor_ui_spike PROPERTIES FOLDER "Tools")
