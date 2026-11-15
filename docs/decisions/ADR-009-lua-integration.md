# ADR-009: Lua integration

**Status:** Proposed (recommendation: Luau; open for one question to the project owner) · **Date:** 2026-11-15 · **Milestone:** 4

## Context
Lua is the second first-class gameplay language (spec 33.1): gameplay scripts, light tooling and
mods "in a defined sandbox/trust model" (spec 5.1), with reload, stack traces and breakpoints where
the debugger allows (spec 7.2). Scripts call the same `noor_rt_*` API as C# (ADR-027).

## Options
- **A. Lua 5.4** (PUC-Rio, MIT). The reference language; tiny; many existing debuggers and
  libraries; sandboxing is possible by hand (custom environments, removing `io`/`os`/`load`).
- **B. Luau** (Roblox, MIT). Lua 5.1-based with additions: built for running untrusted code
  (sandboxed standard library, safe environments), gradual type annotations (good for NoorCode
  autocomplete and errors before running), fast interpreter, actively maintained, CMake build,
  `luau-lsp` (MIT) for editor tooling, VM breakpoint/single-step hooks for a debugger.
  Differences from Lua 5.4: no integer subtype or `goto`, `continue` and compound assignment
  added, some 5.2+ library functions missing.
- **C. LuaJIT 2.1** (MIT). Very fast, but Lua 5.1 semantics, slow upstream releases, and JIT
  limits on some platforms.

## Recommendation
**B, Luau**, because the spec's hardest Lua requirement is the sandbox/trust model for mods, and
Luau was designed for exactly that, while its types make editor tooling much better. The cost is
"Luau, not stock Lua": snippets written for Lua 5.4 may need small changes.

**Question for the project owner (the only product-level part):** is "Luau, a Lua dialect built for sandboxed
game scripting" acceptable as NoorEngine's Lua, or should modders get stock Lua 5.4? Either way,
the engine side (bindings to `noor_rt_*`, reload, error reporting) is the same work.

## Decision (pending the answer above)
- Bind the `noor_rt_*` API by hand-written glue generated from one list of functions (shared with
  the C# bindings), not a general binding library, so both languages stay in step.
- Each scene gets one Lua state; each `script` component with a `.lua`/`.luau` file gets its own
  environment table. Hooks match C#: `on_start`, `on_update(dt)`, `on_fixed_update(dt)`,
  `on_late_update(dt)`, `on_destroy`.
- Reload: changed script files are reloaded on Stop (like C#); hot reload while playing later.

## Reviewed by
Sage (proposal).
