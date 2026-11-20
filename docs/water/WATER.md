# Water

Milestone 8D ([plan](../milestones/M8_PLAN.md#8d-water), [ADR-037](../decisions/ADR-037-water.md)). What works so
far: **part 1, the water surface**, **part 2, waves**, and **part 3, shorelines and reflections**. Underwater, floating
and swimming, rivers and the Water panel are planned (parts 4 to 8); nothing below claims them.

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
| `wave_height` | The main wave, crest to trough, in metres (0: no waves). Three smaller ones come with it. |
| `wave_length` | The main wave's length in metres; the smaller ones are 0.62, 0.38 and 0.24 of it. |
| `wave_direction` | Degrees towards which the waves travel (0 = -Z, 90 = +X, as the wind). The smaller ones cross it at angles. |
| `choppiness` | 0: round, rolling sines; up to 0.9: sharp crests and wide troughs. |
| `wind` | 0..1: how much the environment's ground wind adds (that share of a fully developed sea's height, 0.21 U²/g). |
| `foam` | 0..1: how much foam there is at all (0: none). |
| `foam_depth` | Metres of shallow water where foam gathers: along the shore and round anything standing in the water. |
| `crest_foam` | 0..1: how readily steep crests foam (0: never; towards 1, crests foam before they're steep). Needs `choppiness`. |
| `wet` | 0..1: how much darker wet ground is (under the water and in a band above it). |
| `wet_height` | Metres above where the waves reach that are still wet. |
| `reflections` | 0..1: how much of nearby things is reflected (screen-space; 0: only the sky). |

Bad values are clamped or ignored, never fatal. The built-in presets so far: **Lake** (a calm, clear green lake, a few
specks of foam at the edge) and **Sea** (a 14 m swell 0.6 m high with smaller waves on it, blue-green and deep, rougher
in the wind, foam along the shore and on the steepest crests). The gallery of
presets (Pond, Sea, Tropical, Swamp, River, Deep sea) comes with the Water panel in part 7.

## Waves (part 2)

Waves are a formula, not a simulation (ADR-037): four **Gerstner waves**, the main one from the look and three smaller,
shorter ones crossing it at fixed proportions. Each moves the points of the surface in circles, up and down and a
little sideways (more with `choppiness`), so crests sharpen and troughs widen the way real swell does. Each travels at
deep water's pace for its length (a 14 m wave takes 3 s to pass), and its phase is worked out in double precision, so
an hour in it hasn't drifted.

**One formula, two processors.** The CPU evaluates it for gameplay (`game/water.h`: the height and normal at any point,
found by stepping back from the sideways motion); the vertex shader evaluates the same formula to draw. Gameplay only
ever reads the CPU's answer, a pure function of position and game time with the same sines on every machine, so a
built game floats things exactly where the editor does.

**Scripts** read it where they are:

```lua
local h = noor.water_height(p.x, p.z)   -- the surface's height (world y) there now, waves included; nil: no water
local n = noor.water_normal(p.x, p.z)   -- its normal there
```

```csharp
float? h = Water.Height(p.X, p.Z);
Vector3? n = Water.Normal(p.X, p.Z);
```

Where water bodies overlap, the highest surface answers. Floating physics bodies and swimming come in part 5; until
then a script can float things itself (`tests/data/render_scenes/Scripts/FloatOnWater.luau` bobs buoys on the sea).

**The wind** raises the waves (`wind` × the fully developed sea's height for the ground wind particles feel, weather
zones included as the scene's first camera sees them). It leaves their lengths and directions alone, so a storm rolling
in grows the waves smoothly rather than making them jump.

**The sea's grid** follows the camera: rings round it, each about 2.5% wider than the last, out to 20 km, so a cell
covers about the same part of the screen near and far (0.4 m at your feet, hundreds of metres at the horizon). A box
with waves gets cells of a sixth of its shortest wave (up to 512 a side). Where the grid is too coarse for a wave (fewer
than four cells across it) the drawn wave fades out smoothly instead of shimmering; **gameplay keeps the full waves**,
so far out the drawn surface is calmer than the one scripts read. In the test scene they agree within a centimetre out
to 20 m (measured: 6 mm; 2 mm within 10 m).

What waves don't do yet: break on the shore (they calm down there: part 3, below), push floating things (part 5).
Four waves repeat if you look for it across a big sea; an FFT ocean only if Gerstner waves can't look right.

## Shorelines and reflections (part 3)

**The depth map.** Each water body knows how deep it is over its ground: a grid of samples (the water level minus the
ground's height) built from the scene's **terrains** under it, the highest where they overlap (`game/water.h`,
`water_depth_map`). A box's samples cover its footprint, a sea's cover the terrains (beyond them it's deep); at most 256
a side, at least 0.25 m apart. Gameplay and drawing each build it from the same terrains (the editor's live sculpting
included), so they get the same map, bit for bit; it's rebuilt only when a terrain or the body changes. Only terrains
count: a beach that's a box or a mesh gets foam and wet sand (they come from the picture) but no calmer waves.

**Waves calm in the shallows.** What's left of each wave where the water is `h` deep is `tanh(k h)` (k: its wavenumber;
a Padé fit, exactly 0 at the waterline and 1 from `k h` = 3 on). So long waves feel the bottom first, the shortest ones
last, and nothing moves where the ground is above the water: the sea stays out of the dunes. The depth is read at the
point at rest, bilinearly between samples, with the same arithmetic in the shader (exact texel loads) and on the CPU;
the normal includes the waves growing or shrinking as the bottom shelves. `noor.water_height` and the drawn surface agree
in the shallows as in deep water (measured over a beach: within 7.4 mm). The waves keep their deep-water pace and
direction: they don't slow down, turn to face the shore or break; they get smaller.

**Foam** (`foam`, `foam_depth`, `crest_foam`):
- **at the shore and round things in the water**: where the water is shallower than `foam_depth` (measured from the
  picture: what's under each pixel, so posts, rocks and boxes get it too), stronger where the waves lift the water;
- **on crests**: where the waves squeeze the surface together hard enough (the Jacobian of their sideways motion, from
  the same formula; `crest_foam` sets how soon);
- both are laced with drifting noise in world space, so weak foam breaks up into specks and streaks rather than a
  stripe; lit by the sun (shadowed) and the sky like a rough white surface, under the same fog.

**Wet ground** (`wet`, `wet_height`): before the surface, a flat band just above the highest the waves reach is drawn
over the body (a sea: 400 m round the camera), darkening what's under it by `wet` up to where the waves reach and fading
over `wet_height` above that; the water does the same to what's seen through it. It darkens anything there: a post's
foot, a buoy's waterline. It's a fixed band: it doesn't dry out or get wet again with each wave.

**Reflections of nearby things** (`reflections`): each pixel follows its reflected ray through the copied scene (20
growing steps out to about 75 m, then narrowed down where it first goes behind something) and shows what it hits; it
fades to the sky towards the screen's edges and the march's end, on rough water, and where the reflection is too faint
to see (looking straight down) it isn't traced. What the screen doesn't show (behind the camera, the underside of a
bridge, a roof cut off at the top of the picture) is the sky. Planar reflections (a mirror-perfect lake, drawing the
scene twice) are not in 8D (ADR-037).

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

**What it isn't (yet):** there's no underwater view (part 4), and nothing floats (part 5). The absorption counts the stretch from the surface down as
water and the rest as air, so it's an approximation, as ADR-037 says.

**Measured** (RTX 3080, 1280×720, 4× MSAA, fastest of 3 × 300 frames, `tools/samples/measure_hillside_lake.py`):

| Scene | GPU | Scene | Scene copy | See-through (water) |
|---|---|---|---|---|
| Hillside lake | 1.24 ms | 0.53 ms | 0.020 ms | 0.057 ms |
| Hillside lake without its water | 1.15 ms | 0.50 ms | 0 | 0.022 ms |
| Test pool | 0.31 ms | 0.15 ms | 0.017 ms | 0.039 ms |
| Test pool without its water | 0.25 ms | 0.12 ms | 0 | 0.018 ms |
| Wavy sea (part 2, the Sea preset, about 670,000 vertices) | 0.28 ms | 0.09 ms | 0.016 ms | 0.113 ms |
| Wavy sea without its water | 0.15 ms | 0.06 ms | 0 | 0.018 ms |

Part 3 (2026-11-20, the same way, `measure_hillside_lake.py`): foam, wet ground and screen-space reflections added.

| Scene | GPU | Scene | Scene copy | See-through (water) |
|---|---|---|---|---|
| Hillside lake | 1.33 ms | 0.53 ms | 0.020 ms | 0.141 ms |
| Hillside lake without its water | 1.15 ms | 0.50 ms | 0 | 0.022 ms |
| Test pool | 0.34 ms | 0.14 ms | 0.017 ms | 0.062 ms |
| Test pool without its water | 0.25 ms | 0.12 ms | 0 | 0.018 ms |
| Wavy sea (the Sea preset) | 0.49 ms | 0.09 ms | 0.016 ms | 0.319 ms |
| Wavy sea without its water | 0.15 ms | 0.06 ms | 0 | 0.018 ms |
| Shore (`Water_Shore`: the sea round a sandy coast) | 0.67 ms | 0.13 ms | 0.016 ms | 0.330 ms |
| Shore without its water | 0.32 ms | 0.10 ms | 0 | 0.017 ms |
| Reflecting pond (`Water_Reflection`) | 0.46 ms | 0.10 ms | 0.015 ms | 0.171 ms |
| Reflecting pond without its water | 0.27 ms | 0.08 ms | 0 | 0.017 ms |

The wavy sea's own pass went from 0.11 to 0.32 ms; switching each part off on it (`reflections`, `foam`, `wet` 0):
reflections 0.14 ms, foam 0.03 ms, wet ground under 0.01 ms; the rest (depth-map reads, settings) about 0.03 ms.

In part 2, the copy cost about 0.02 ms, a lake about 0.04 ms and the wavy sea about 0.1 ms; splitting the pass (keeping the MSAA images for the second
half) adds about 0.03 ms to the scene pass. About 0.1 ms in all. The GPU's pass timings now list **Scene**, **Scene
copy** and **See-through** separately (the Render panel and the player's report).

## Tests

- Unit tests "water" (6): transmittance and absorption from a tint against values worked out by hand, the Lake preset
  worked through from its sRGB colours, fields clamped and ignored, `.noorwater` files checked, and a component becoming
  the renderer's surface (position, size by scale, turn, preset under its own fields, a sea).
- Unit tests for waves (3): deep water's pace and phases an hour in, the wind's share, the height found at a point is
  where the waves moved the surface (never more than the four amplitudes), the normal against the measured slope, and
  scripts reading the highest body through the World.
- Goldens: `Water_Pool` (a tiled pool with a ramp floor, a ball half in it, a block on the deep end), `Water_Sea` (a
  beach, a pier and a rock in an endless sea), `Hillside_Lake` (the Hillside lake), and `Water_Waves_1s` and `_4s` (the
  Sea preset with Lua-floated buoys, one and four seconds in).
- `render.water_readback`: the drawn surface read back from the depth buffer (`noor_player --msaa 1 --water-probes 24`)
  against `water_height`: within 1 cm out to 20 m.
- `player.water_waves`: the buoys float identically in the project and in the built game.
- Unit tests for shorelines (3): what's left of a wave by depth (the fit against tanh, its slope, long waves first,
  half a metre of water keeping every wave to its share, dry ground not moving at all, deep water off the map), the
  normal over a shelving bottom against the measured slope, the depth read bilinearly, and depth maps from a flat
  terrain (a pond's samples and bounds, turned, a sea over its terrain, none off the terrain or without one, the same
  map asked again).
- `render.water_readback_shore`: the same readback over a beach (`Water_Shore_Probe`): only pixels at least 0.3 m deep
  count, at least 60 of them shallower than 2.5 m (where the waves are calmed); within 1 cm out to 20 m.
- Goldens `Water_Shore` (the Sea preset round a sandy coast with three posts in the shallows) and `Water_Reflection` (a
  pond with a house on the far bank, its roof running off the top of the picture). The part 1 and 2 goldens were redrawn
  (reflections, foam and wet ground in them now).
- The editor smoke test: Create ▸ Water, the viewport draws it, one undo removes it.
