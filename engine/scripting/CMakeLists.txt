# scripting: script languages as runtime systems (ADR-027): Lua via Luau (ADR-009), and the
# engine half of C# (ADR-008; the managed half is engine/scripting/dotnet/NoorEngine.Scripting).
noor_add_module(scripting
    SOURCES
        src/lua_scripts.cpp
        src/csharp_scripts.cpp
        src/dotnet_host.cpp
    PUBLIC_DEPS
        noor::runtime
        noor::physics
        noor::audio
    PRIVATE_DEPS
        Luau.VM
        Luau.Compiler
)

# The managed half, for games: NoorEngine.Scripting.dll + its runtimeconfig.json in
# ${NOOR_MANAGED_DIR}. noor_cli build packs it with a game's compiled scripts; noor_player starts
# .NET with it (dotnet_host.cpp). NoorEditor builds its own copy through its ProjectReference.
find_program(NOOR_DOTNET dotnet)
set(NOOR_MANAGED_DIR ${CMAKE_BINARY_DIR}/managed CACHE INTERNAL "NoorEngine.Scripting for games")
if(NOOR_DOTNET)
    set(project_dir ${CMAKE_CURRENT_SOURCE_DIR}/dotnet/NoorEngine.Scripting)
    file(GLOB managed_sources CONFIGURE_DEPENDS ${project_dir}/*.cs ${project_dir}/*.csproj)
    add_custom_command(
        OUTPUT ${NOOR_MANAGED_DIR}/NoorEngine.Scripting.dll ${NOOR_MANAGED_DIR}/NoorEngine.Scripting.runtimeconfig.json
        COMMAND ${CMAKE_COMMAND} -E env DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
                ${NOOR_DOTNET} build ${project_dir}/NoorEngine.Scripting.csproj -c Release -nologo -v quiet -clp:NoSummary
                -o ${NOOR_MANAGED_DIR}
        DEPENDS ${managed_sources}
        COMMENT "Building NoorEngine.Scripting (C# scripts in games)"
        VERBATIM)
    add_custom_target(noor_scripting_managed ALL DEPENDS ${NOOR_MANAGED_DIR}/NoorEngine.Scripting.dll)
else()
    message(STATUS "dotnet not found: games built here can't run C# scripts (install the .NET 10 SDK)")
endif()
