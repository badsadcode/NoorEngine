# ADR-022: Math and coordinate conventions

**Status:** Accepted
**Date:** 2026-11-15

## Context
Every system (rendering, physics, terrain, import, procedural tools) must agree on handedness,
units, axes and matrix layout. The spec (section 15.4) forbids one tool assuming centimeters
while another assumes meters. Getting this wrong later means a migration of every asset.

## Decision
| Topic | Convention |
|---|---|
| Handedness | **Right-handed** |
| Up axis | **+Y** |
| Forward | Cameras and characters look down **−Z**; +X is right |
| Units | **Meters**, **seconds**, **kilograms**; angles in **radians** in code (degrees only in UI) |
| Vectors | Column vectors: `v' = M * v` |
| Matrix storage | **Column-major** (`m[column][row]`), matching HLSL's default packing; shaders use `mul(M, v)` |
| Transform order | `world = T * R * S` (scale first, then rotate, then translate) |
| Clip space | Vulkan: x right, **y down**, depth 0..1. The projection flips Y so world +Y is up on screen |
| Depth | **Reverse-Z** with an infinite far plane: near = 1, far = 0, compare `GREATER`, clear to 0 |
| Triangle winding | Counter-clockwise front faces in world space |
| Color math | Linear color in shaders; sRGB only at texture sampling / swapchain write |

Why reverse-Z: with a 32-bit float depth buffer it spreads precision nearly evenly across
distance, which matters for the 32–64 km worlds in the spec. An infinite far plane removes one
tuning knob.

Large-world precision (double positions, camera-relative rendering) is **ADR-013** and still
open; `noor::math` is single-precision for now and will gain double types there.

## Consequences
- Importers convert from source conventions (glTF is right-handed +Y up meters, same as ours;
  FBX/Blender files often are not) and record what they converted.
- Tests in `tests/unit/test_math.cpp` lock these conventions in.

## Reviewed by
Sage.
