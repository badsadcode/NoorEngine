# noor_editor_api: shared library (DLL) exposing a C ABI for NoorEditor (ADR-025).
add_library(noor_editor_api SHARED
    src/editor_api.cpp
    src/viewport.cpp
    src/document.cpp
    src/document_api.cpp
    src/document_animation.cpp
    src/document_tilemap.cpp
    src/viewport_tools.cpp)
target_include_directories(noor_editor_api PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_definitions(noor_editor_api PRIVATE NOOR_EDITOR_API_BUILD)
target_link_libraries(noor_editor_api PRIVATE noor::foundation noor::platform noor::render noor::asset noor::scene noor::runtime noor::physics noor::audio noor::scripting noor::game noor::csg noor::terrain noor::terrain_edit noor::procgraph)
noor_set_target_options(noor_editor_api)
