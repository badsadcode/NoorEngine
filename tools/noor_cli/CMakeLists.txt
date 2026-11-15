add_executable(noor_cli main.cpp)
target_link_libraries(noor_cli PRIVATE noor::foundation noor::asset noor::scene)
noor_set_target_options(noor_cli)
set_target_properties(noor_cli PROPERTIES FOLDER "Tools")
