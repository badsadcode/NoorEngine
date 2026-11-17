# Fails if the configure-time link walk (tests/CMakeLists.txt) found editor-only code in a runtime target.
file(READ "${REPORT}" report)
if(NOT report STREQUAL "")
    message(FATAL_ERROR "Editor-only code reaches the runtime:\n${report}")
endif()
message(STATUS "noor_player and the runtime modules link no CSG or Manifold code")
