# ADR-030: Materials: assets, instances and how they reach the renderer

**Status:** Proposed · **Date:** 2026-11-17 · **Milestone:** 5

## Context
Today a surface's look is a colour: vertex colours from GLB base-colour factors, or a
`primitive` component's `color`. Spec 11 wants a professional material workflow: base materials,
material instances, parameter presets, live preview under HDRIs, and eventually a node graph with
procedural nodes, layering and painting (11.1–11.4). Milestone 5's deliverable is "material
assets, texture set import, a material editor MVP with **extensible graph architecture**". The
graph editor itself is a big tool; the data and renderer side must not paint us into a corner.

## Options
1. **Format**: a JSON material asset (diffable, like scenes, ADR-005); or materials only inside
   models and scenes.
2. **Instances**: a material that names a parent and overrides some values (like prefab overrides,
   ADR-024); or copies.
3. **Graph now or later**: build the node graph first and generate shaders from it; or start with
   one parameter-driven "standard" material that the graph will later feed.

## Decision
- **`.noormat`** JSON asset with a stable GUID (`.noormeta` like every asset):
  ```json
  { "noor_material": 1, "shading": "standard",
    "base_color": [1, 1, 1, 1], "base_color_map": "<texture guid>",
    "metallic": 0, "roughness": 0.5, "metallic_roughness_map": null,
    "normal_map": null, "normal_scale": 1, "occlusion_map": null,
    "emissive": [0, 0, 0], "emissive_map": null, "emissive_strength": 1,
    "alpha": "opaque" | "cutout" | "blend", "alpha_cutoff": 0.5,
    "uv_scale": [1, 1], "uv_offset": [0, 0], "triplanar": false, "double_sided": false }
  ```
  Channels and ranges follow glTF's metallic-roughness model (ADR-012); each is documented with
  its colour space in `docs/rendering/MATERIALS.md`.
- **Instances**: `{ "noor_material": 1, "parent": "<material guid>", "overrides": { "base_color": …,
  "roughness": … } }`. The engine resolves the chain on load; a changed parent updates every
  instance (hot reload, like models).
- **Where materials come from**:
  - GLB materials become sub-assets of the model (read-only); "Make editable" writes a `.noormat`
    copy and points the model's override at it.
  - **"Create material from texture set"**: pick a NoorMaterial export (manifest) or any folder
    of maps; files are matched by role from the manifest or by name, settings set by ADR-029.
  - `primitive` components get a `material` field (a GUID); `color` stays as the simple case.
- **Graph later, data now**: the standard material is the renderer contract. The future node graph
  (spec 11.1/11.3, its own ADR when it starts) **outputs the same channels**: either baked to
  textures (procedural nodes evaluated to maps, the NoorMaterial way) or compiled to a shader
  variant that fills those channels. Either way scenes, instances and the renderer don't change.
  The MVP editor in Milestone 5 is a parameter editor with a live preview, not a graph.
- **Renderer side**: materials become a GPU table (one entry per loaded material: factors and
  bindless texture indices, ADR-012); draws carry a material index. A missing texture shows a
  visible checker and logs once; it never crashes or goes silently black.

## Consequences
- Easier: everything (GLB, NoorMaterial, primitives) lands on one model; instances make "the
  same wall, darker" cheap; diffs are readable.
- Harder: the graph editor has to fit an existing contract rather than invent one; layered and
  painted materials (moss on a wall, spec 11.4/11.8) need either more channels or a layer
  extension, decided when painting starts (Milestone 6 with CSG).
- Must do: hot reload of `.noormat` and textures; the Inspector edits materials (with undo) and
  shows a preview sphere; Noor Interchange (ADR-023) gets a note on how a GLB's materials and a
  NoorMaterial set relate (proposal to the spec, not a silent divergence).

## Evidence / benchmark
None yet.

## Migration or reversal strategy
`noor_material` is versioned with migrations (ADR-005); instances only store overrides, so new
channels get defaults without touching files.

## Reviewed by
Sage (proposal). Tom: plan review.
