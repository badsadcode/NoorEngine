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
`rock_nor_gl_1k.png` an OpenGL one. The **last** role word decides, because set names can
contain role words: `wooden_rough_planks_diff_1k.jpg` is a colour map and `blue_metal_plate_arm_1k.jpg`
an ORM map. Poly Haven's `<set>_<role>_<size>` names work as they are, with Make material too.)

```json
"settings": { "kind": "normal", "normal_convention": "opengl", "mips": true, "max_size": 4096,
              "compression": "auto" }
```

Change the settings in the editor: double-click a texture in the Content Browser (or **Edit**) for
its **import settings** tab: kind, normal convention, compression, max size, mipmaps, and what
the texture costs on the GPU with them (and without compression). Changes go into the
`.noormeta` at once and the viewport reloads the texture. Editing the `.noormeta` by hand works
too. `kind`
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
5. Stored for the GPU, **block-compressed** by default (`"compression": "auto"`):

   | Kind | Format | Bytes per pixel |
   |---|---|---|
   | `color` | BC7 sRGB (perceptual error weights) | 1 |
   | `data` (ORM, masks) | BC7 (all channels weighted the same) | 1 |
   | `normal` | BC5: X and Y only; the shader rebuilds Z = √(1 − X² − Y²) | 1 |
   | `hdr` | RGBA16F, never compressed | 8 |

   `"compression": "none"` keeps RGBA8 (4 bytes per pixel): pixel-exact, for pixel art or when a
   compression artefact shows. Any size works: the edge blocks of sizes that aren't multiples of
   4 are padded (Vulkan allows it). A 2048² map with mips takes 5.3 MB instead of 21 MB.
   Encoding is done on the CPU on all cores with bc7enc and rgbcx (Richard Geldreich, MIT or
   public domain): about a third of a second per 1254² map in a release build, once; then it's
   cached. Measured quality: BC7 colour over 38 dB PSNR on the test picture, BC5 normals within
   3° (unit tests), and no visible difference on a real PBR set (mean 0.06/255 per channel in the
   rendered picture). A GPU without BC support gets the pixels decoded back to RGBA8 on load.

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
BC6H for HDR textures, BC4 for single-channel maps (they're BC7 now: same size as BC4's double),
cooking compressed textures into built games (they compress on first start, then cache), texture
streaming, `.exr`, glTF images in separate files, thumbnails in the Content Browser.
