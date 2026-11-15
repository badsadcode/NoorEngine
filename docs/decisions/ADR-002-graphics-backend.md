# ADR-002: Graphics backend

**Status:** Accepted (for Milestone 0–1; renderer architecture gets its own ADR)
**Date:** 2026-11-15

## Context
We need one solid, modern, debuggable backend first. The spec warns against several
half-working renderers. Windows is the first platform; Linux should remain possible.

## Options
- **A: Vulkan 1.3.** Cross-platform (Windows, Linux, Android; macOS via MoltenVK), explicit,
  excellent validation layers and tools (RenderDoc, Nsight). Verbose.
- **B: Direct3D 12.** Excellent Windows tooling, but Windows/Xbox only.
- **C: Abstraction library (NVRHI, Diligent, bgfx, WebGPU/Dawn).** Faster start, but the
  renderer's core design would be shaped by someone else's abstraction.

## Decision
**Vulkan 1.3 as the single backend**, with dynamic rendering and synchronization2 as baseline
features. Our own thin `noor::render` RHI layer sits on top so a second backend remains
possible later without rewriting the renderer. Shaders: HLSL compiled to SPIR-V (DXC ships
with the Vulkan SDK); Slang is a candidate to evaluate when the material system starts.

Validation layers are on by default in debug builds and in the diagnostics tool, and any
validation error fails the smoke test.

## Consequences
- Minimum GPU: one with a Vulkan 1.3 driver (roughly NVIDIA GTX 900+, AMD GCN 4+, Intel Gen9+).
- macOS is "not supported" until MoltenVK is tested; we do not claim it.
- Memory allocation will use VMA (MIT) once we allocate buffers/images (Milestone 1).

## Evidence / benchmark
Milestone 0 `noor_diag` creates a 1.3 device, swapchain and clears frames with zero validation
errors on the dev machine (RTX 3080). See devlog #002.

## Migration or reversal strategy
The RHI boundary keeps Vulkan types out of engine-facing headers above `render/`.

## Reviewed by
Sage.
