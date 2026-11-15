# ADR-010: Physics stack

**Status:** Accepted · **Date:** 2026-11-15 · **Milestone:** 4

## Context
Spec 19.1/19.2: rigid bodies (static, dynamic, kinematic), colliders, layers and masks, raycasts
and shape queries, triggers, joints, CCD where needed, a fixed tick with interpolation, and a
capsule character controller (slopes, steps, jumping, moving platforms). "Start with a vetted
solver rather than an untested physics engine from first principles."

## Options
- **A. Jolt Physics** (MIT). Modern, multithreaded, used in shipped AAA games and as Godot 4's
  physics option; has `CharacterVirtual` (a capsule controller with slopes/steps/platforms),
  optional cross-platform determinism, broad-phase layers, good documentation and samples.
- **B. NVIDIA PhysX 5** (BSD-3). Very complete; large dependency, heavier integration, GPU features
  we don't need.
- **C. Bullet 3** (zlib). Familiar but largely unmaintained for games.

## Decision
**A, Jolt Physics**, pinned by commit (ADR-021), built from source with CMake, as the
`physics` runtime system:
- Components: `rigidbody` (`static` | `dynamic` | `kinematic`, mass, friction, restitution, layer),
  `collider` (`box`, `sphere`, `capsule`, `mesh` for static geometry). Static mesh colliders cook
  from the GLB meshes, and Noor Interchange `-colonly` collision nodes are used when present.
- Fixed tick 60 Hz after the scripts' fixed update (ADR-027); rendered transforms interpolate
  between ticks.
- `character` component on `CharacterVirtual`: move/jump input from scripts, ground state, slope
  limit, step height.
- Script API: raycast, overlap, apply force/impulse, velocity, trigger enter/exit events.
- Determinism: Jolt's cross-platform determinism mode stays **off** (it costs performance); results
  are repeatable on one machine and build. Nothing claims more (spec 19.5).

## Consequences
- 2D physics (Box2D, MIT) is a separate future decision with 2D games.
- Editing colliders in the editor (gizmo shapes) arrives with the inspector work in this milestone.

## Reviewed by
Sage.
