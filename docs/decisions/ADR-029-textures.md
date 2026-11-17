# ADR-029: Textures: import, colour spaces, mips and GPU formats

**Status:** Proposed · **Date:** 2026-11-17 · **Milestone:** 5

## Context
Textures were deferred from Milestone 2 to here (M2 review). Spec 10.4 and 11.7: 8-bit and
high-bit-depth sources, correct mipmaps, normal map import, channel packing, GPU-native formats,
memory estimates, and "a material should not mysteriously change appearance after import because
a normal map was interpreted as sRGB". Sources we already know we'll get: images embedded in GLB
files, PNG sets from **NoorMaterial** (albedo, normal, roughness, AO, metallic, 16-bit height;
naming presets for generic/Godot/Unity/Unreal, DirectX or OpenGL normals, ORM packing; a
`<set>.set.json` manifest stating colour space, normal convention, bit depth and channels), and
HDRIs (`.hdr`, `.exr`) for environments.

## Options
1. **Decoders**: **stb_image** (public domain/MIT, already the stb family we use: PNG 8/16-bit,
   JPEG, TGA, BMP, Radiance .hdr) plus **tinyexr** (BSD-3) for OpenEXR; or **OpenImageIO**
   (BSD-3, very complete, many dependencies).
2. **GPU formats**: uncompressed RGBA8/RGBA16F with mips; or block compression (BC7 colour/data,
   BC5 normals, BC6H HDR) cooked at import.
3. **Where settings live**: in the texture's `.noormeta` (per file, like model import settings) or
   in the material that uses it.

## Decision
- **stb_image + tinyexr**, pinned like every dependency (ADR-021).
- **A texture is an asset** with import settings in its `.noormeta`:
  `kind` = `color` (sRGB) | `data` (linear: roughness, metallic, AO, masks) | `normal` | `hdr`;
  `normal_convention` = `opengl` (+Y, glTF's) | `directx` (−Y, flipped on import); `mips`;
  `max_size`; later `compression`. Defaults are guessed from the name and the bit depth
  (`*_normal*` → normal, `*_rough*` → data, …) and from a NoorMaterial manifest when one sits
  next to the files; the guess is shown and can be changed.
- **Mips** are generated at import, filtered in linear light (sRGB decoded first); normal-map mips
  are renormalised.
- **GPU formats, first**: RGBA8 (sRGB or UNORM by kind), RG8/RGBA8 for normals, RGBA16F for HDR,
  with full mip chains. **Block compression is a later part** (BC7/BC5/BC6H), decided after
  measuring memory on the sample; encoder candidates are bc7enc_rdo (MIT/public domain) and
  DirectXTex (MIT). Memory per texture is shown in the Inspector from day one.
- **GLB textures** become sub-assets of their model (like its meshes), with settings by role (the
  base colour is `color`, the normal texture `normal`, metallic-roughness `data`).
- **Derived data** (decoded, mipped, later compressed) is cached in `.noor/cache`, keyed by the
  source's content hash, the settings and the importer version; deleting the cache is always safe.
  Builds (ADR-028) carry sources for now; cooked builds are still later.

## Consequences
- Easier: NoorMaterial sets import with the right colour spaces and normal convention without
  the user knowing what those are.
- Harder: uncompressed textures are big (a 2048² RGBA8 with mips ≈ 21 MB of GPU memory); the
  sample stays small until compression lands, and the memory column keeps us honest.
- Must do: a texture preview in the Content Browser and Inspector (with the colour-space choice
  visible), and render tests that would catch a normal map read as sRGB.

## Evidence / benchmark
None yet. Decoder licences: stb (public domain or MIT), tinyexr (BSD-3-Clause); both go in
`third_party/manifest.json` with pinned commits and hashes.

## Migration or reversal strategy
Settings live in `.noormeta`, so changing decoders or adding compression re-imports from the
untouched sources; nothing in scenes or materials refers to a GPU format.

## Reviewed by
Sage (proposal). Tom: plan review.
