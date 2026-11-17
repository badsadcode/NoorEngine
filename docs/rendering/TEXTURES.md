# Textures

How images become textures (ADR-029). Status: **Milestone 5, part 2**. Textures are used by
`primitive` components today; materials (`.noormat`, part 3) will use them the same way.

## Importing

Put image files anywhere under `Assets/`: **.png** (8 or 16-bit), **.jpg/.jpeg**, **.tga**,
**.bmp**, **.hdr**. The registry gives each one a `.noormeta` with its import settings, guessed
from the file name the first time:

| File name contains | Kind | Stored as |
|---|---|---|
| `albedo`, `basecolor`, `diffuse`, anything else | `color` | sRGB (the GPU decodes it to linear when sampling) |
| `normal`, `nrm`, `nor`, `norm` | `normal` | linear; `dx`/`directx` in the name: green flipped on import |
| `roughness`, `rough`, `metallic`, `metal`, `ao`, `occlusion`, `orm`, `height`, `depth`, `displacement`, `mask`, `cavity`, `curvature`, `opacity`… | `data` | linear |
| `.hdr` files | `hdr` | 16-bit float |

(Words are split on `_`, `-`, `.` and spaces, so `Rock_Normal_DX.png` is a DirectX normal map and
`rock_nor_gl_1k.png` an OpenGL one.)

```json
"settings": { "kind": "normal", "normal_convention": "opengl", "mips": true, "max_size": 4096 }
```

Change the settings by editing the `.noormeta` (an editor panel for it is still to come). `kind`
matters: a normal map imported as `color` would be "gamma decoded" and point the wrong way,
and a roughness map imported as `color` comes out darker.

**Normal maps** use the OpenGL convention (+Y up the texture), like glTF, Blender, Godot and
NoorMaterial. Maps made for DirectX engines (Unreal) need `"normal_convention": "directx"`.

**Check a texture** without the editor:
```
noor_cli texture Assets/Textures/*.png
```
prints the kind, size, mip count and GPU memory of each file, as the engine would import it.

## What import does

1. Decode (stb_image). Colour images are converted from sRGB to linear light.
2. DirectX normal maps: green flipped.
3. Larger than `max_size`: halved until they fit.
4. Mipmaps down to 1×1, **averaged in linear light** (a black-and-white checker fades to the right
   mid-grey, not a too-dark one); normal map mips are renormalised to unit length.
5. Stored for the GPU: RGBA8 sRGB (colour), RGBA8 (data, normal), RGBA16F (hdr). Uncompressed
   for now: a 2048² texture with mips is about 21 MB of GPU memory. Block compression comes in
   part 7, once the sample scene's numbers say how much it's needed.

The result is cached in `<project>/.noor/cache/textures/`, keyed by the file's content, its
settings and the importer version, so a texture is processed once. Deleting the cache is always
safe. Built games carry the source images and decode them on start (cooking is later).

## Using textures

Textures are used by **materials** ([MATERIALS.md](MATERIALS.md)): a `.noormat` names them, and
**Make material** turns a whole texture set into one. A `primitive` component can also name
textures directly (the material fields described in place):

```json
"primitive": { "shape": "box", "size": [2, 2, 0.4], "color": "#B85A3C",
               "base_color_map": "<texture id>", "normal_map": "<texture id>",
               "normal_scale": 1, "roughness": 0.9, "metallic": 0, "uv_scale": [2, 2] }
```

`color` multiplies the base colour texture. In the editor, **Content Browser ▸ a texture ▸ Add
to scene** makes a cube wearing it (as its colour, or as its normal map if the name says
normal). The built-in box, sphere and capsule have UVs; boxes map the texture once per face.

A texture that can't be loaded shows a magenta-and-black checker and logs a warning once.

## In the renderer

All textures live in one **bindless table** (4096 slots, ADR-012): the shader picks a texture by
its slot number, so any mix of textures needs no special shader. Slot 0 is white, 1 a flat normal,
2 the missing-texture checker. One sampler: trilinear, repeat, 8× anisotropic when supported.

Textures inside GLB files are decoded the same way (their kind follows how the material uses
them) and cached by content. Changing a texture file or its `.noormeta` reloads it in the editor
and in running games within half a second.

## Not yet
Texture compression (BC7/BC5/BC6H), `.exr` (with environments, part 5), an import-settings panel
in the editor, texture streaming, glTF images in separate files.
