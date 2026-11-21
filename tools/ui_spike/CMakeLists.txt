# The UI spike (gameplay foundations, part 6; ADR-041): the same pause menu with our own widgets and with RmlUi, drawn
# into pictures by one CPU rasterizer. Not part of the engine: only with -DNOOR_UI_SPIKE=ON. RmlUi and FreeType come from
# cmake/NoorDependencies.cmake, like the engine's.
add_executable(noor_ui_spike main.cpp own_ui.cpp rml_ui.cpp)
target_link_libraries(noor_ui_spike PRIVATE noor::game noor::runtime RmlUi::RmlUi stb::image)
target_compile_features(noor_ui_spike PRIVATE cxx_std_23)
set_target_properties(noor_ui_spike PROPERTIES FOLDER "Tools")
