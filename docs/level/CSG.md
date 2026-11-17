# CSG brushes

How NoorEngine builds level geometry from brushes (ADR-016). Status: **Milestone 6, parts 1–3**: the
kernel and the shapes, brushes in the editor, and materials and UVs per face.

## In the editor

**Create ▸ Brush ▸ Box / Wedge / Cylinder / Cone / Sphere / Stairs / Arch.** With nothing (or
something unrelated) selected, this makes a **CSG model** entity and the brush inside it. With a
CSG model or one of its brushes selected, the new brush goes into that model. One undo step.

- **A brush is an entity** with a `brush` component, a child of the model. Move, rotate and scale
  it with the usual gizmos and snapping; edit its numbers in the Inspector: shape and operation
  from lists, size, sides, steps, a material from the project, UV scale/offset/rotation.
- **The order of the model's children is the order of operations.** Edit ▸ Move Up / Move Down
  (Alt+Up / Alt+Down) moves the selected brush earlier or later: a doorway moved before its wall
  cuts nothing.
- **Subtract and intersect brushes** show as see-through red and blue shapes while editing (not
  while playing), so they can be seen and clicked. Clicking a face of the result selects the brush
  that made it.
- Everything is undoable and saved in the scene like any other entity.

```json
"brush": { "shape": "box", "operation": "add", "size": [4, 3, 0.3], "sides": 16, "steps": 8,
           "material": "<material id>", "uv_scale": [1, 1], "uv_offset": [0, 0], "uv_rotation": 0,
           "uv_align": "world",
           "faces": [null, null, null, null, { "material": "<another id>", "uv_align": "brush" }, null] }
```

### Faces

Every logical face (see the table above) can have its **own material and UV settings**; a face
without them uses the brush's. In the editor:

- **Click a face** of the selected brush in the viewport (the first click on a model selects the
  model, the next one the brush and the face under the pointer). The face is outlined in cyan and
  comes first, open, in the Inspector's **Faces** section.
- Each face: a material (or "the brush's", or none), and under **UV**: repeats per metre, offset,
  rotation and alignment, plus the **texel density** ("1024 px per metre: a 1024 px texture that
  repeats every 1 m") so walls next to each other can be kept equally sharp. **Use the brush's**
  removes the face's own settings.
- **Alignment:** `world` (the default) projects along the model's axes, so textures stay put in
  space and line up across cuts; `brush` projects along the brush's own axes, so a wall turned 37°
  keeps level rows of bricks instead of sheared ones.

Faces keep their settings through cuts: cut a window into a wall whose front is brick and the
front stays brick; the window's sides come from the window brush.

The document rebuilds a model when its brushes change (cached per model, keyed by what it was
built from). While a gizmo is being dragged the brush moves as it is; the cut updates when you
let go. A small house rebuilds in a few milliseconds in a release build.

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
Dragging a material from the Content Browser onto a face (the native viewport doesn't take drops
yet: pick it in the Inspector), triplanar projection, UV tools per triangle, live re-cutting while a
gizmo is dragged, cooked meshes for games (part 4: the player doesn't draw CSG models yet),
collision and GLB export (part 4), brush outlines for add brushes, nested models.
