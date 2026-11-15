# scripting: script languages as runtime systems (ADR-027). Lua via Luau (ADR-009) now;
# C# (ADR-008) joins in Milestone 4, part 4.
noor_add_module(scripting
    SOURCES
        src/lua_scripts.cpp
    PUBLIC_DEPS
        noor::runtime
    PRIVATE_DEPS
        Luau.VM
        Luau.Compiler
)
