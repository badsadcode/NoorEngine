# ADR-016: CSG kernel: brushes, booleans, materials and UVs

**Status:** Accepted (Milestone 6, part 1 spike passed) · **Date:** 2026-11-17 · **Milestone:** 6

## Context
Spec 12 asks for level building inside the editor: brush primitives, union/subtract/intersect
with an editable history, every face with its own material, new faces after a cut getting
"sensible material coordinates", stability under rotation and scale, and a clear line between
editable authoring data and cooked runtime meshes (12.7). The decision register names this
ADR-016: "geometry robustness, UV/material inheritance, editable source".

## Options
- **A: Manifold** (github.com/elalish/manifold, Apache-2.0, C++17). Robust mesh booleans with
  guaranteed manifold output; used by Godot 4.4+ for its CSG nodes and by OpenSCAD. Triangles
  keep a face/run id through the boolean, so we can map every output triangle back to its source
  brush face. Costs: a sizeable dependency (optional TBB, Clipper2 only for 2D cross-sections),
  Apache-2.0 NOTICE obligations (compatible with MIT), meshes only (no exact planes).
- **B: Own plane-based CSG** (Quake/TrenchBroom-style convex brushes, BSP splitting). Exact planes
  in double precision, trivial per-face material/UV, small code. Costs: only convex brushes
  (cylinders, arches and stairs become several brushes), T-junctions and slivers to clean up
  ourselves, years of edge cases that Manifold has already paid for.
- **C: Polygon BSP CSG** (csg.js style). Tiny and easy, but known to produce slivers and to blow up
  polygon counts on repeated operations; not robust enough for editable history.

## Decision
**A, Manifold** (v3.5.4), behind our own `noor_csg` interface. The spike (Milestone 6, part 1)
passed every case it set: a doorway through a wall, rotated and non-uniformly scaled brushes, coplanar
faces (two walls touching), a brush entirely inside another, 100 subtracts on one wall, and timings
(see Evidence). **B** stays the fallback if real levels find cases Manifold can't handle.

Shared regardless of the kernel:
- **Brushes are entities** with a `brush` component (shape + exact parameters, operation, face
  materials and UV settings) under a `csg_model` entity; the order of children is the operation
  history. Saving, undo, prefabs and duplication work as for any entity.
- **Materials and UVs follow source faces**: each brush face has a material and a projection
  (world-aligned planar or box, offset/scale/rotation). Projections are anchored in world space
  (or the model's space), so a face cut out of a wall keeps lining up with the bricks around it,
  and faces exposed by a subtract take the cutting brush's face settings.
- **Cooked output is derived data**: triangles, normals, tangents, submeshes per material and a
  collision mesh, cached by a hash of the model's brushes; `noor_cli build` packs the cooked mesh.
  Games never run the kernel.

## Consequences
- Easier: robust booleans of any closed shape (cylinders, spheres, arches) without convex splitting.
- Harder: a third-party geometry core whose failure modes we must learn; mesh-only data (no exact
  planes) means face-picking and UV editing work on triangles grouped by source face.
- Must: pin Manifold by commit and hash (ADR-021), add it to `third_party/manifest.json` with its
  Apache-2.0 NOTICE, and keep it out of runtime modules (a CMake/test check).

## Evidence / benchmark
Spike, `tests/unit/test_csg.cpp` (5 cases, 193 checks, all passing on the first run of the
kernel):
- every shape (box, wedge, cylinder, cone, sphere, stairs, arch) is a closed solid with its exact
  analytic volume (polygonal ones to 1e-4, the sphere to 2%);
- a doorway whose bottom is coplanar with the wall's: exact volume; the wall's front keeps
  material 1 with UVs exactly `(x, -y) × scale`, as before the cut; the jambs are the door's
  faces (material 2) and lie inside the wall's thickness;
- a wall turned 37° with a non-uniformly scaled cutter, and a cutter tilted 20° (volume
  `0.075 / cos 20°` to 1e-3), and a mirrored (negative scale) brush;
- touching boxes leave no inside face; a box inside a box makes a hollow; box ∩ sphere;
- a 10 m wall with 100 windows: exact volume, the same bytes when built twice, **~14 ms in a
  release build** (285 ms in Debug), 2,012 triangles.

How materials and UVs survive: Manifold carries each input mesh's original id and a per-triangle
face id through the booleans; `noor_csg` maps them back to (brush, face) and computes normals, box
projection UVs (in model space) and tangents after the boolean.

Also in place from part 1: `boundary.runtime_has_no_editor_code`, a test that walks the link graph
of `noor_player` and the runtime modules and fails if `noor_csg` or Manifold appear (and checks
that the walk does find them in the unit tests, so the guard can fail).

## Migration or reversal strategy
Scenes store brushes (shape parameters, operations, materials), never kernel data. Switching
kernels changes how cooked meshes are made, not the saved files. Cooked meshes are rebuilt from
the scene at any time.

## Reviewed by
Sage: proposed before the spike, accepted after it (2026-11-17).
