# Materials

How surfaces get their look (ADR-030). Status: **Milestone 5, parts 3 and 6** (materials, and the
material editor).

## A material file: `.noormat`

Anywhere under `Assets/`. Every field is optional; missing ones take the defaults shown.

```json
{
  "noor_material": 1,
  "base_color": [1, 1, 1, 1],          "base_color_map": "<texture id>",
  "metallic": 0, "roughness": 0.5,
  "metallic_roughness_map": "<id>",    // packed like glTF: G = roughness, B = metallic (ORM maps fit)
  "roughness_map": "<id>",             // or single-channel maps (red)
  "metallic_map": "<id>",
  "normal_map": "<id>", "normal_scale": 1,
  "occlusion_map": "<id>", "occlusion_strength": 1,
  "emissive": [0, 0, 0], "emissive_map": "<id>", "emissive_strength": 1,
  "alpha": "opaque",                   // or "cutout" (leaves, fences); "blend" is drawn as cutout for now
  "alpha_cutoff": 0.5,
  "double_sided": false,
  "uv_scale": [1, 1], "uv_offset": [0, 0]
}
```

| Channel | Meaning | Range | Colour space |
|---|---|---|---|
| `base_color` × map | Albedo for non-metals, reflection colour for metals | 0..1 (real materials ~0.04–0.9) | linear numbers; the map is a `color` texture (sRGB file) |
| `metallic` × map | 0 non-metal, 1 metal; in between only at edges | 0..1 | `data` texture |
| `roughness` × map | 0 mirror, 1 matte (perceptual) | 0..1 | `data` texture |
| `normal_map` | Tangent-space directions, OpenGL convention (+Y up) | | `normal` texture |
| `occlusion_map` | Ambient occlusion (dims ambient light only) | 0..1 | `data` texture, red channel |
| `emissive` × map × strength | Light the surface gives off | ≥ 0 | linear; the map is `color` |

Factors multiply their maps, so with a roughness map, set `"roughness": 1` to use the map as is.
**Emissive** is relative to the camera's exposure for now (1 is about white on screen, whatever
the EV); physical units come with lights.

Texture ids are the textures' asset ids (in their `.noormeta`). Missing textures are logged and
left out; a material that can't be read at all shows a magenta checker.

## Instances

"The same, but…": an instance names a parent and changes only some fields.

```json
{ "noor_material": 1, "parent": "<material id>",
  "overrides": { "base_color": [0.3, 0.3, 0.3, 1], "uv_scale": [4, 4], "normal_map": null } }
```

Everything else comes from the parent (and its parent, up to 16 deep; loops are an error). `null`
removes a parent's texture. Change the parent and every instance follows.

## Where materials come from

- **Texture sets**: in the editor, select a texture in the Content Browser and press **Make
  material**, or run `noor_cli make-material <project> <texture>`. Files beside it that share its
  name before the role word are picked up:

  | Name part | Role |
  |---|---|
  | `albedo`, `basecolor`, `base_color`, `diffuse`, `color` | base colour map |
  | `normal`, `nrm`, `nor` | normal map |
  | `orm`, `arm` | occlusion + roughness + metallic (one packed map) |
  | `roughness`, `rough` / `metallic`, `metal` | single-channel maps |
  | `ao`, `occlusion` | occlusion map |
  | `emissive`, `emission` | emissive map |

  `bricks_albedo.png`, `bricks_normal.png` and `bricks_orm.png` become `bricks.noormat`. Previews
  and thumbnails are skipped. An existing `.noormat` is never overwritten.
- **GLB models** bring their own materials: every glTF primitive is drawn with its material
  (textures inside the GLB included; base colour factors are in the vertex colours).
- **By hand**: write a `.noormat`, or an instance of one.

## The material editor

Double-click a material in the Content Browser (or select it and press **Edit**). It opens as a
tab next to the viewport:

- **Preview**: a sphere, cube or plane wearing the material, in its own viewport (orbit it like
  the scene view). Lit by the default sky and sun or by any `.hdr` in the project, with an
  exposure slider (EV 8–17) and a slider that turns the environment.
- **Fields**: every channel above. Colours are typed as sRGB hex, like in a paint program, and
  saved as linear numbers. Texture slots list the project's textures, or `(none)`.
- **Saving**: there is no Save button. Each change is written to the `.noormat` a moment later,
  so the preview, the scene and running games all read the same file. **Ctrl+Z / Ctrl+Y** undo
  and redo inside the tab (a slider drag is one step). A change made to the file outside the
  editor (a text editor, git) is picked up.
- **Instances**: **Save as instance** writes `<name>_variant.noormat` next to the material, as an
  instance that changes nothing yet, and opens it. An instance shows its parents' values. Fields
  it changes are cyan with a **↺** that gives the field back to the parent. Choosing `(none)` for
  a texture hides the parent's texture (a `null` override). **Open parent** opens the parent.

The engine side is small. `noor_doc_set_preview` publishes a one-object scene on the document's
second snapshot channel, and `noor_viewport_set_preview` points a viewport at it (editor API v11).
A new `reload` number in the settings makes the preview re-read materials and textures at once,
instead of waiting for the half-second check.

Not yet: material thumbnails in the Content Browser, editing several materials at once (one
preview per document: the tab in front owns it), and the texture import-settings panel.

## Using materials

On a `primitive` component: `"material": "<material id>"`. Its `color` then tints the material only
if it is given. Without a `material`, a primitive's own fields are a material described in
place (`base_color_map`, `normal_map`, `roughness`, … the same names as above).

In the editor, **Add to scene** on a material makes a ball wearing it.

**Editing**: change a `.noormat` (or a texture) on disk and the editor and running games pick it up
within half a second.

## In the renderer

Materials live in a table of 4096 on the GPU (`render::MaterialDesc`); a draw names its material by
number, like textures (ADR-012). Double-sided materials use a second pipeline without back-face
culling; cutout ones discard pixels below `alpha_cutoff`.

## Not yet
See-through blending (sorted, after the opaque scene), material graphs (later, ADR-030), painting and layers (with CSG, Milestone 6), clear coat, sheen,
transmission, a second UV set, external image files referenced by glTF (only images inside GLBs).
