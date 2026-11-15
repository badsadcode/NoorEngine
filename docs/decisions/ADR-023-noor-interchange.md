# ADR-023: Noor Interchange: how Noor tools exchange assets

**Status:** Proposed (v0.1 draft; becomes Accepted once two tools export it and NoorEngine imports it)
**Date:** 2026-11-15

## Context
Bad Ideas Works builds several independent projects in parallel:
- **NoorMD** and its benches (NoorForm trees/rocks, NoorMaterial PBR, NoorMesh, NoorForge, NoorSound...),
- procedural generators being renamed to the Noor family (BadShip → NoorShip, BadLevel → NoorLevel,
  BadBuilding → NoorArchitect, and planned NoorVegetation, NoorRoads, NoorBridges),
- **NoorEngine**.

They move at different speeds and are polished one at a time. Tom needs them to fit together
without managing them as one giant project. The NoorEngine spec (sections 5.4, 15.4, 15.5) asks
for shared data contracts and headless generators, and forbids unit/axis disagreements.

## Options
- **A: Merge into one codebase / call each other's code.** Tight coupling: one unfinished bench
  blocks the engine; every refactor breaks someone.
- **B: Shared code library used by all.** Useful later for common algorithms, but forces the same
  language/runtime (NoorLevel is Python) and couples release cycles.
- **C: File-based contract.** Every tool exports standard GLB plus a small JSON manifest and
  recipe, following one written spec. Tools stay independent; anything that reads glTF can use
  the output.

## Decision
**C: Noor Interchange** ([`docs/interchange/NOOR_INTERCHANGE.md`](../interchange/NOOR_INTERCHANGE.md)):
- GLB as the single model format; standard glTF features first (`KHR_lights_punctual` for lights);
- Noor data in node `extras.noor` (markers, semantic parts, LODs, collision), with readable node
  names as a fallback;
- a `<name>.noor.json` manifest per asset, validated by [`noor.asset.schema.json`](../interchange/noor.asset.schema.json);
- a recipe with seed for regeneration; stable asset `id` stored in the recipe;
- ADR-022 space conventions (right-handed, +Y up, metres; assets face +Z, bottom-centre pivot).

The spec lives in the NoorEngine repository for now because NoorEngine is the main consumer and
the repository is public. It can move to a studio-level repository later without changing content.

## Consequences
- NoorEngine's asset importer (Milestone 2) reads Noor Interchange natively: markers become
  entities, collision nodes become colliders, `id` becomes the asset GUID.
- Each generator/bench needs an exporter update: write the manifest, adopt marker names, keep a
  stable `id` in its recipe. Existing GLB output mostly fits already.
- Godot/Blender get most of it for free (`extras` → metadata/custom properties, KHR lights);
  Unity/Unreal need a small import script, which can be published once for all tools.
- Changes to the spec go through a PR to `NOOR_INTERCHANGE.md`, with a version bump.

## Evidence
Two example manifests validate against the schema; the schema rejects wrong units, Z-up, unknown
kinds, paths escaping the folder and missing ids (checked with `jsonschema`, Draft 2020-12).

**First exporter:** BadBuilding (commit 6d59174, its decision D14) exports v0.1. Its feedback
produced v0.2: collision is decided by `extras.noor.collision` with Godot's `-colonly` suffix
(plain `-col` stayed visible in Godot), LODs as one file per level, new `passage` marker and a
clearer `entrance`/`door` split, derived UUID v5 ids, tool-specific manifest blocks.
NoorEngine does not import it yet, so the status stays Proposed.

## Reviewed by
Sage. Tom chose GLB as the common format and asked for consistent metadata across all tools.
