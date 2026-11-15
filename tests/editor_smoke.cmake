# Builds NoorEditor against the noor_editor_api.dll of this build and runs its --smoke self-test.
# Needs a GPU (it opens editor windows with a Vulkan viewport) and the .NET SDK.
# Inputs: DOTNET, PROJECT (NoorEditor.csproj), NATIVE_DIR (folder with noor_editor_api.dll),
#         OUT (results + screenshots), DATA (tests/data), CONFIG (Debug/Release).

foreach(var DOTNET PROJECT NATIVE_DIR OUT DATA CONFIG)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "editor_smoke.cmake: ${var} is not set")
    endif()
endforeach()

set(bin "${OUT}/bin")
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")

execute_process(
    COMMAND "${DOTNET}" build "${PROJECT}" -c ${CONFIG} -o "${bin}" --nologo -v quiet "-p:NoorNativeDir=${NATIVE_DIR}/"
    RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "dotnet build of NoorEditor failed (${build_result})")
endif()

execute_process(
    COMMAND "${bin}/NoorEditor.exe" --smoke "${OUT}" --test-data "${DATA}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_log)
message("${run_output}")
if(NOT run_result EQUAL 0)
    message("${run_log}")
    message(FATAL_ERROR "NoorEditor smoke test failed (${run_result}); see ${OUT}/editor_results.json")
endif()
