# Water

Milestone 8D ([plan](../milestones/M8_PLAN.md#8d-water), [ADR-037](../decisions/ADR-037-water.md)). What works so
far: **part 1, the water surface**. Waves, shorelines with foam, screen-space reflections, underwater, floating and
swimming, rivers and the Water panel are planned (parts 2 to 8); nothing below claims them.

## The `water` component (part 1)

Water is a component on an entity, like a light or a terrain:

```json
"water": {"shape": "box", "preset": "Lake", "size": [32, 32], "roughness": 0.1}
```

- **`shape`**: `"box"` (the default): a level rectangle `size` metres across ([x, z], times the entity's scale) at the
  entity's position, turned with the entity about +Y. A lake, a pond, a pool. `"sea"`: level at the entity's height,
  under the camera out to the horizon (20 km; the haze hides the edge). Rivers (splines) come in part 6.
- **Its look**: a built-in `preset`, or `asset` (a `.noorwater` file's id), with any of the look's fields on the
  component itself over them, the way environments and effects work.
- **Where the water shows**: everywhere on the surface where the ground is lower than it. Put the rectangle's edges on
  ground above the water (or past the camera's view), or you'll see a straight edge. The Hillside lake
  (`tools/samples/add_hillside_lake.py`) digs a hollow in its own sculpt layer and checks that every point of the
  rectangle's edge is on higher ground.

In the editor: **Create ▸ Water** makes a 20 × 20 m Lake; the Inspector picks the preset from the engine's list and
edits size and any field you add. The Render panel counts water surfaces drawn.

## The look (`.noorwater`)

```json
{"noor_water": 1, "name": "Lake", "preset": "Lake",
 "water": {"tint": "#6FB7A8", "clarity": 4.0, "scatter": "#1F4A48", "refraction": 0.06, "shore_fade": 0.12,
           "roughness": 0.06, "ripple": 1.0, "ripple_scale": 1.2}}
```

| Field | What it does |
|---|---|
| `absorption` | Light lost per metre of water, per colour (linear rgb). Red goes first, then green: that's why deep water is blue-green and the bottom of a lake goes green, then dark. |
| `tint` + `clarity` | The friendlier way to say the same: the colour white light has become after `clarity` metres. `absorption = -ln(tint) / clarity` per channel (the tint is sRGB, linearised first). |
| `scatter` | The colour light scattered back from inside the water takes on, lit by the sun and the sky (an albedo: dark for clear water, light for milky). It's what you see where the water is too deep to see the bottom. |
| `refraction` | How far what's under is bent by the ripples (a fraction of the screen at a metre deep). |
| `shore_fade` | Metres of depth over which the water fades in at the shore: no hard line where it meets the ground. |
| `roughness` | Blurs the sky's reflection and spreads the sun's glint. |
| `ripple`, `ripple_scale` | Small ripples' strength (0: a mirror) and size in metres. |

Bad values are clamped or ignored, never fatal. The built-in presets so far: **Lake** (a calm, clear green lake). The
gallery of presets (Pond, Sea, Tropical, Swamp, River, Deep sea) comes with the Water panel in part 7.

## How it's drawn

When water is in view, the frame's scene pass is split in two:

1. the opaque scene and the sky are drawn as before;
2. **the scene is copied once**: its colour (the resolved HDR image) and its depth (with MSAA: one sample of each pixel,
   resolved by the GPU at the end of the pass; without: copied) go to two images the water samples;
3. the see-through pass draws the water first (it writes depth), then glass and particles over it as before.

Without water, nothing changes: one pass, no copies.

Each water pixel then knows what's under it (the copied depth turned back into a world position) and works out:

- **how much water the light went through**: down from the surface to what's under it (the vertical depth, the way
  sunlight came in) and back up along the view ray; each colour is kept by `exp(-absorption × metres)` (Beer–Lambert);
- **what's under it, bent**: the copied colour looked up a little off to the side by the ripples' slope (more where
  it's deeper), unless that lands on something in front of the water (no ghost of the ball in the water beside it);
- **light scattered inside the water** (`scatter`, lit by the sun where it isn't in shadow, and the sky) filling in
  what was absorbed;
- **the sky reflected** by Fresnel (Schlick, 2% looking straight down, nearly everything at grazing angles), from the
  environment's cube blurred by roughness, and **the sun's glint** (GGX);
- **the air between** the camera and the surface (aerial perspective and fog) over the water's own light;
- **the shore**: the water fades in over the first `shore_fade` metres of depth.

The ripples are a few sine waves in world space moving with the game's clock, so built games and goldens agree.
Ripples smaller than a few pixels fade out, and roughen the reflection instead (otherwise they alias into a grid at a
distance).

**What it isn't (yet):** the surface is flat apart from the ripples' normals (waves: part 2); it reflects only the sky
and sun, so a lake doesn't show the house beside it (screen-space reflections: part 3); there's no foam (part 3), no
underwater view (part 4), and nothing floats (part 5). The absorption counts the stretch from the surface down as
water and the rest as air, so it's an approximation, as ADR-037 says.

**Measured** (RTX 3080, 1280×720, 4× MSAA, fastest of 3 × 300 frames, `tools/samples/measure_hillside_lake.py`):

| Scene | GPU | Scene | Scene copy | See-through (water) |
|---|---|---|---|---|
| Hillside lake | 1.24 ms | 0.53 ms | 0.020 ms | 0.057 ms |
| Hillside lake without its water | 1.15 ms | 0.50 ms | 0 | 0.022 ms |
| Test pool | 0.31 ms | 0.15 ms | 0.017 ms | 0.039 ms |
| Test pool without its water | 0.25 ms | 0.12 ms | 0 | 0.018 ms |

So the copy costs about 0.02 ms and a lake about 0.04 ms; splitting the pass (keeping the MSAA images for the second
half) adds about 0.03 ms to the scene pass. About 0.1 ms in all. The GPU's pass timings now list **Scene**, **Scene
copy** and **See-through** separately (the Render panel and the player's report).

## Tests

- Unit tests "water" (6): transmittance and absorption from a tint against values worked out by hand, the Lake preset
  worked through from its sRGB colours, fields clamped and ignored, `.noorwater` files checked, and a component becoming
  the renderer's surface (position, size by scale, turn, preset under its own fields, a sea).
- Goldens: `Water_Pool` (a tiled pool with a ramp floor, a ball half in it, a block on the deep end), `Water_Sea` (a
  beach, a pier and a rock in an endless sea) and `Hillside_Lake` (the Hillside lake).
- The editor smoke test: Create ▸ Water, the viewport draws it, one undo removes it.
