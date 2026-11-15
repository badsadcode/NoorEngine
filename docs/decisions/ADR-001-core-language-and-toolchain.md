# ADR-001: Core language and toolchain

**Status:** Accepted
**Date:** 2026-11-15

## Context
The engine core needs predictable performance, direct access to graphics APIs and mature
libraries (Vulkan, SDL, Jolt, Tracy). The team develops on Windows 10/11 with Visual Studio 2022.
Linux must stay possible later, so nothing in the build may be Windows-only by construction.

## Options
- **A: C++23 (conservative subset) + CMake.** Industry standard for engines; every candidate
  library is C/C++; CMake works with VS, Ninja, CLion, VS Code and CI.
- **B: C++20 only.** Safer compiler coverage, but no `std::expected`, which we want for
  error handling everywhere.
- **C: Rust.** Excellent safety, but weaker ecosystem for editors/tools we plan to embed and
  awkward .NET/Lua/C++-library interop for a small team.

## Decision
**C++23, restricted to features supported by MSVC 19.4x, GCC 13+ and Clang 17+** (notably
`std::expected`, `std::format`, `std::source_location`). Build with **CMake ≥ 3.28** using
`CMakePresets.json`. Primary compiler: **MSVC (VS 2022)**. Warnings are errors on our own code.

Third-party dependencies are fetched with **CMake FetchContent pinned to exact commit hashes**
(see ADR-021 and `third_party/manifest.json`). We will revisit vcpkg when the dependency count
makes source builds slow; switching is a build-script change, not a code change.

## Consequences
- No exceptions crossing module or plugin boundaries; recoverable errors use `noor::Result<T>`.
- Engine code may use exceptions internally only where a library forces it; default is none.
- Must add a Clang or GCC CI job before claiming Linux readiness.

## Evidence / benchmark
Toolchain verified on the dev machine: VS 2022 17.14, CMake 4.1, Vulkan SDK 1.4.363.

## Migration or reversal strategy
Dropping to C++20 would require a small `expected` polyfill; contained in `foundation/result.h`.

## Reviewed by
Sage (engineering). Tom and Noor informed.
