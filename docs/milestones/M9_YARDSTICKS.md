# Milestone 9's yardsticks

**Why they exist:** Milestone 9 adds big worlds, streaming, generation and simulation. Tom and Noor's principle 0
([M9 plan](M9_PLAN.md#principle-0-noorengine-stays-a-general-purpose-engine)) says small games must not pay for any of
it. These numbers are what four different kinds of project cost **before** 9A changed anything (2026-11-20, 9A part 1).
After every part of Milestone 9 the test suite runs them again (`yardstick.*`), and a project that got slower or bigger
beyond its limit fails the build.

## The projects

| Yardstick | Project | What it stands for |
|---|---|---|
| cube | `tests/data/yardstick_cube` | The smallest possible game: one cube, a camera, a sun |
| pong | [`samples/NoorPong`](../../samples/NoorPong) | A 2D game: an orthographic camera, a pixel-art sprite, Lua, a HUD |
| fps | [`samples/NoorFPS-lite`](../../samples/NoorFPS-lite) (`Main_Lua`) | A compact hand-made 3D level: physics, a player, Lua |
| hillside | [`samples/Hillside`](../../samples/Hillside) (`Hillside`) | A procedural landscape: graph terrain, scattered forest |
| platformer | [`samples/NoorPlatformer`](../../samples/NoorPlatformer) | A pixel-art 2D game at 320 × 180: a tilemap, the platformer character, Lua (added in 2D part 7) |
| isometric | [`samples/NoorIsometric`](../../samples/NoorIsometric) | An isometric tilemap room, an 8-direction sprite, point lights (added in 2D part 7) |
| *(from 9A part 5)* | the streamed test world | Big worlds themselves |

## The baseline (2026-11-20, before 9A)

Ten seconds of each in `noor_player` (RelWithDebInfo) at 1280×720 on an RTX 3080, Windows 10; the median of three runs
(`tools/samples/measure_yardsticks.py`). **CPU per frame** is the game update, gathering the view and recording the
frame together (what the CPU spends, vsync aside); its 95th percentile in brackets.

| Yardstick | CPU/frame median (p95) | GPU/frame | Working set | Private | GPU resident | Threads | First frame |
|---|---|---|---|---|---|---|---|
| A single cube | 0.19 ms (0.27) | 2.07 ms | 196 MB | 420 MB | 117 MB | 30 | 0.67 s |
| NoorPong (2D) | 0.28 ms (0.37) | 1.23 ms | 197 MB | 418 MB | 117 MB | 30 | 0.65 s |
| NoorFPS-lite | 0.43 ms (0.60) | 2.16 ms | 199 MB | 418 MB | 121 MB | 30 | 0.71 s |
| Hillside | 6.21 ms (7.13) | 4.80 ms | 219 MB | 507 MB | 199 MB | 30 | 1.27 s |
| NoorPlatformer (2026-11-21) | 0.39 ms (0.59) | 0.36 ms | 198 MB | 353 MB | 68 MB | 30 | 0.64 s |
| NoorIsometric (2026-11-21) | 0.23 ms (0.41) | 1.01 ms | 197 MB | 419 MB | 118 MB | 30 | 0.62 s |

**The limits** (`tests/yardsticks.json`) are each measurement plus a margin for a busy machine: CPU median ×1.3 + 0.2 ms,
CPU p95 ×1.5 + 0.4 ms, GPU ×1.3 + 0.1 ms, memory ×1.15 + 24 MB, GPU memory ×1.05 + 8 MB, threads +2, first frame ×1.5 +
0.5 s. The timing tests run alone (`RUN_SERIAL`), and each starts with a short warm-up launch, so
the first frame is measured **warm**: in the first full run with these tests, NoorFPS-lite took 9 s to its first frame
straight after the suite's C# build of it (build servers still busy, disk cache cold) and 0.7 s the next time. **The check is proven to bite**: `yardstick.catches_a_stall` runs the
cube with a deliberate 1 ms stall in every update (`noor_player --stall-ms 1`) and passes only if the check reports it
over its limit (it measures 1.15 ms against a limit of 0.45 ms).

Re-measure (only when a change is *meant* to change these, and say why in the changelog):
`python tools/samples/measure_yardsticks.py`. Compare without rewriting: `--check`.

## What the baseline already shows

Measuring the smallest games for the first time found costs they pay today, before Milestone 9 adds anything. None of
them is new; all of them go against principle 0, so they're written down here and in the ideas inbox rather than
explained away.

1. **Every game carries about 117 MB of GPU render targets**, a cube and Pong included: a 4× MSAA HDR scene at
   1280×720, its depth, the scene copy water reads, and four 2048² shadow cascades, which are allocated even when the
   scene has shadows off (Pong). Shadow maps on demand, and lighter targets for games that don't need them, would give
   small games most of that back.
2. **The see-through pass costs about 0.3 ms with nothing see-through in it** (the cube and Pong have no glass, water
   or particles). Not explained yet.
3. **An idle game uses 30 threads** (the GPU driver's, audio, Jolt's job system, the file watcher...). Not a problem in
   itself; the yardstick watches that Milestone 9's loading threads only appear in games that stream.
4. **The GPU times of tiny scenes are noisy and pessimistic**: the cube's 2 ms is mostly the GPU clocking down because
   it has so little to do (its passes add up to about 1 ms). The limits allow for it; for real GPU costs use a scene
   with real work in it.
5. **Hillside spends 6 ms of CPU a frame**, and not where I expected. Gathering the scene (`collect_view`) takes only
   0.17 ms; **5.1 ms goes to building the draw list from it** (`SceneRenderer::draws`: the scattered forest, tree by
   tree, a level of detail chosen for each one every frame). So 9A part 3 has to cache the draw list too, not just the
   gather, and Hillside, being a yardstick, should get faster when it does.
