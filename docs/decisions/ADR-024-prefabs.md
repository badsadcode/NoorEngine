# ADR-024: Prefabs

**Status:** Accepted
**Date:** 2026-11-15

## Context
Spec 9.1: prefabs are reusable entity trees with overrides, nested prefabs, variants and update
propagation. Scenes must stay diffable (ADR-005) and survive prefab edits without breaking.

## Options
- **A: Expand on save.** The scene file stores every prefab entity, marked as "from prefab".
  Simple to read, but every prefab edit rewrites every scene, and Git diffs explode.
- **B: Store only the instance and its overrides; expand on load.** Scene files stay small and only
  change when the instance changes. Prefab edits propagate automatically. Needs stable ids for
  entities that are never written to the scene.
- **C: Copy-paste with a link.** No real propagation.

## Decision
**B.**
- A prefab is an asset file, `<name>.noorprefab`: `{ "noor_prefab": 1, "id", "name", "entities": [...] }`,
  entities in scene format (ADR-004), root first. Its GUID is its `id`.
- An **instance** is one ordinary scene entity (the *placement*: name, parent, transform) with a
  `prefab` component: `{ "asset": <prefab GUID>, "removed": [...], "overrides": {...} }`.
- **Expansion** (on load, never saved) puts the prefab's entities under the placement entity. Each
  expanded entity's id is **derived**: `Guid::derive(instance id, prefab-local id)` (a SHA-256
  based, RFC 9562 version 8 UUID). Same instance + same prefab entity = same id, on every load and
  every machine, so references to expanded entities stay valid.
- **Overrides** are keyed by the prefab-local entity id and may set `name`, `layer`, `tags`,
  `transform` (any of `position` / `rotation` / `scale`) and individual `components` (a `null`
  value removes a component). `removed` lists prefab-local ids to leave out, with their children.
- **Update propagation:** edit the prefab, and every instance changes on the next load, except
  the properties it overrides.
- **Nested prefabs:** prefab entities may have their own `prefab` component; expansion recurses.
  **Variants** fall out of this: a prefab whose root is an instance of another prefab plus overrides.
  Cycles (A contains B contains A) are detected and reported, never expanded forever (depth ≤ 16).
- An override whose target no longer exists in the prefab is kept in the file and reported as a
  warning, so an accidental prefab edit can be undone without losing the scene's overrides.

## Consequences
- Overrides are just component data, so undo/redo (scene::History) covers them for free.
- Adding new child entities *inside* an instance's expanded tree is not supported yet (planned).
- Tools that only need the authored scene can ignore prefabs; tools that render need the expansion.

## Reviewed by
Sage.
