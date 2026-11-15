# scripting: script languages as runtime systems (ADR-027): Lua via Luau (ADR-009), and the
# engine half of C# (ADR-008; the managed half is engine/scripting/dotnet/NoorEngine.Scripting).
noor_add_module(scripting
    SOURCES
        src/lua_scripts.cpp
        src/csharp_scripts.cpp
    PUBLIC_DEPS
        noor::runtime
        noor::physics
    PRIVATE_DEPS
        Luau.VM
        Luau.Compiler
)
