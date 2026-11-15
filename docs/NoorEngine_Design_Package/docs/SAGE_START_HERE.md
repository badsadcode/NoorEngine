# Sage, start here: NoorEngine

**Bad Ideas Works — engineering handoff**

This is a deliberately ambitious **target product specification**. The user wants an independently designed game engine with a polished editor, native/managed modular tools, C# and Lua, a built-in IDE, terrain/voxel and procedural world tooling, materials/CSG/animation/FX/audio/networking, broad asset support, sample games and templates. They want it built slowly **without throwaway shortcuts**, but that does not prohibit automated tests or internal builds.

## Your first job is not to implement every feature

Read `../NOORENGINE_MASTER_SPEC.md`. Then:

1. Summarize a minimal viable **engine architecture** that supports the long-term features without forcing them all into the core.
2. Identify and challenge problematic assumptions or incompatible requirements.
3. Present concrete choices and trade-offs for the graphics backend, GUI framework, scene/component model, asset pipeline, C#/Lua embedding, SDK/plugin ABI and build/test stack.
4. Produce short Architecture Decision Records (ADRs) for important decisions. Mark unresolved questions honestly.
5. Establish a clean repository/build/test/logging baseline and one real foundation increment.
6. Keep an honest technical changelog, public devlog and meaningful test coverage from day one.

## Product non-negotiables

- **Own architecture**; external open-source ideas/libraries are welcome with license checks.
- **Beautiful, fast NoorEditor GUI**; no default barebones debug UI as the main application.
- **Professional NoorCode IDE** built in, including C# and Lua tooling/debugging; external IDEs also supported.
- **Optional modules/plugins** for generation tools, with stable contracts; no monolithic procedural code in core.
- **Non-destructive authoring** for materials, CSG and terrain where feasible.
- **Reliable projects, versioned assets, imports, reimports, migrations and backups**.
- **Tests, logging, profiling and documentation** are part of being production-quality, not shortcuts.
- **No magical claims**. Mark future features as planned and concept imagery as concept imagery.

## Initial reply we want from you

- Proposed architecture diagram, with clear module boundaries.
- Top technology choices with alternatives/risks and licensing notes.
- The first implementation step and how it can be tested.
- Three to five highest-risk decisions worth settling early.
- Concrete next actions and any critical missing information.

## Do not

- Start by creating 50 empty DLLs merely to check boxes.
- Rewrite existing unrelated games to use NoorEngine.
- Build a dummy "engine" that will be thrown away next week.
- Fake editor screenshots, unimplemented APIs or performance numbers.
- Promise full Linux/macOS/mobile/console capability without a support plan.
- Assume native DLLs are secure simply because they have a manifest.
- Conflate "no test versions" with "no testing".

**Bonus rule:** If the codebase becomes incomprehensible, the goblin in charge of documentation gets to throw a chair. Figuratively.
