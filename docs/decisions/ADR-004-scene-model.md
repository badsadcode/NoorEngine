# ADR-004: Scene model

**Status:** Accepted (authoring model; runtime storage may change behind it)
**Date:** 2026-11-15

## Context
Spec 9.1 asks for entities with stable IDs, an optional hierarchy, typed components and prefabs,
without forcing beginners to understand ECS internals. The first content is imported GLB models
(buildings, ships, levels) whose nodes carry Noor Interchange data.

## Options
- **A: Entity + component documents.** Entities with GUIDs, names, parent, transform and a set of
  typed components. Simple to serialize, inspect and diff.
- **B: Pure ECS (archetypes) from day one.** Fast at scale, but harder to author, serialize and
  explain; premature before we have systems that need it.
- **C: Object-oriented node tree** (like Godot nodes). Familiar, but components as subclasses
  get rigid with plugins.

## Decision
**A for the authoring/serialized model.** A runtime may later pack the same data into ECS
storage for speed (spec 9.1 explicitly allows it) without changing files.

- **Entity:** `id` (GUID), `name`, `parent` (entity GUID or none), `transform` (position,
  rotation as quaternion `[x, y, z, w]`, scale; local to the parent), `components`.
- **Components** are keyed by type name. First built-in types:
  `mesh` (asset GUID + mesh name inside it), `marker` (a Noor Interchange marker block),
  `collider` (shape + asset GUID/mesh), `light` (later).
- **Unknown component types are kept verbatim** so plugins' data survives editors that don't
  know them (spec 5.3, 8.5).
- **Scene file** `.noorscene`: `{ "noor_scene": 1, "id", "name", "entities": [...] }`, entities in
  authoring order (parents before children).
- **Instantiating a model** (e.g. a BadBuilding GLB) creates one entity per glTF node, keeping the
  node's Noor id in the entity name and its `extras.noor` data in `marker`/`collider` components.
  Collision nodes never get a `mesh` component (spec 3.5 of Noor Interchange).

Prefabs (reusable entity trees with overrides) build on this and get their own ADR in Milestone 2.

## Consequences
- Transforms are float32 for now; large-world precision is ADR-013 (open).
- No scripting/behaviour on entities yet (Milestone 4).

## Reviewed by
Sage.
