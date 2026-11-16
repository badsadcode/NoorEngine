# A C# compile error stops noor_cli build before it writes anything, and the error names the file
# and line the way the editor does ("Scripts/Broken.cs:4: CS1002: ...").
#   -DCLI=<noor_cli> -DPROJECT=<player_project_csharp> -DWORK=<scratch folder>
file(REMOVE_RECURSE "${WORK}")
file(COPY "${PROJECT}/" DESTINATION "${WORK}/project")
file(REMOVE_RECURSE "${WORK}/project/.noor")
file(WRITE "${WORK}/project/Scripts/Broken.cs" "using NoorEngine;\npublic class Broken : NoorScript\n{\n    public override void OnUpdate(float dt) { int x = }\n}\n")
execute_process(COMMAND "${CLI}" build "${WORK}/project" "${WORK}/out" RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
message(STATUS "${out}${err}")
if(code EQUAL 0)
    message(FATAL_ERROR "a project with a C# compile error built")
endif()
string(FIND "${out}${err}" "Scripts/Broken.cs:4: CS" at)
if(at EQUAL -1)
    message(FATAL_ERROR "the compile error doesn't say Scripts/Broken.cs:4")
endif()
if(EXISTS "${WORK}/out")
    message(FATAL_ERROR "a failed build wrote into the output folder")
endif()
