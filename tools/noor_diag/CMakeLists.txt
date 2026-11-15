add_executable(noor_diag main.cpp)
target_link_libraries(noor_diag PRIVATE noor::foundation noor::platform noor::render)
noor_set_target_options(noor_diag)
set_target_properties(noor_diag PROPERTIES FOLDER "Tools")
