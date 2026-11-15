# noor_editor_api: shared library (DLL) exposing a C ABI for NoorEditor.
add_library(noor_editor_api SHARED src/editor_api.cpp src/viewport.cpp)
target_include_directories(noor_editor_api PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_definitions(noor_editor_api PRIVATE NOOR_EDITOR_API_BUILD)
target_link_libraries(noor_editor_api PRIVATE noor::foundation noor::platform noor::render)
noor_set_target_options(noor_editor_api)
