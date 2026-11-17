# CSG brushes

How NoorEngine builds level geometry from brushes (ADR-016). Status: **Milestone 6, part 1**: the
kernel and the shapes, as an engine module with tests. Brushes in the editor come in part 2.

## Brushes

A brush is a shape with exact parameters and an operation:

| Shape | Parameters | Logical faces |
|---|---|---|
| Box | size | +X, -X, top, bottom, front (+Z), back (-Z) |
| Wedge | size (high at the back, low at the front) | slope, bottom, back, +X side, -X side |
| Cylinder | size, sides (axis Y) | side (smooth), top, bottom |
| Cone | size, sides (apex up) | side (smooth), bottom |
| Sphere | size, sides (an ellipsoid when the size isn't equal) | surface (smooth) |
| Stairs | size, steps (rising towards -Z) | treads, risers, sides, back, bottom |
| Arch | size, sides (a rectangle with a half circle on top: a doorway shape) | front, back, sides and bottom, curve (smooth) |

Operations, applied in order: **add** (union), **subtract**, **intersect**. The first *add* starts
the solid; a subtract before anything exists does nothing. The result of a list of brushes is
deterministic: the same brushes give the same bytes.

## Materials and UVs

Every logical face has a **surface**: a material slot, and UV scale (repeats per metre), offset
and rotation. A brush has one surface for all faces and can override any face.

UVs are a **box projection in the model's space**: a face pointing mostly along X takes its UVs
from Z and Y, along Z from X and Y, and up/down from X and Z, oriented so the texture reads the
right way round from outside. Because the projection is anchored in space rather than on the
brush:
- cutting a doorway leaves the wall's texture exactly where it was;
- the faces a subtract creates (the door's jambs) take the **cutting brush's** surfaces, already
  lined up with the world;
- moving the whole model doesn't make textures slide.

Curved faces (cylinder and cone sides, spheres, arch curves) get smooth normals; the rest have hard
edges. Tangents follow +U, so normal maps work on every face.

## The kernel

Booleans are done by [Manifold](https://github.com/elalish/manifold) (Apache-2.0, pinned v3.5.4),
the library Godot's CSG uses. Manifold returns, for every output triangle, which input mesh and
which input face it came from; `noor_csg` turns that into materials, normals, UVs and tangents.
Measured: a wall with 100 windows rebuilds in ~14 ms (release).

## The runtime boundary

`noor_csg` and Manifold are for the editor and for cooking only. Games will load the cooked
meshes (part 4). The test `boundary.runtime_has_no_editor_code` fails if either ever gets linked
into `noor_player` or a runtime module.

## Not yet
Brushes in scenes and the editor (part 2), per-face material editing and UV tools (part 3),
cooked meshes, collision and GLB export (part 4).
