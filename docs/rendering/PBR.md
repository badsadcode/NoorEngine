# Lighting and shading (PBR)

How NoorEngine turns light into pixels (ADR-012). Status: **Milestone 5, parts 1–4** (frame, sun
and sky, materials in [MATERIALS.md](MATERIALS.md), shadows, point and spot lights). Skies from
HDRIs come in part 5.

## The frame

```
shadow pass ── casters from the sun ──▶ 4 shadow cascades (depth)
scene pass  ── lit draws ─────────────▶ HDR image (RGBA 16-bit float, already exposed)
screen pass ── tonemap ───────────────▶ the screen (sRGB)  ── editor gizmos ── HUD
```

0. **Shadows**: everything that casts a shadow is drawn from the sun into four shadow maps (see
   below).
1. **Scene**: every mesh is lit by the sun and the sky in physical units and multiplied by the
   camera's exposure, into a 16-bit float image. Where nothing is drawn, the sky colour.
2. **Tonemap**: one full-screen triangle maps the HDR image to display colours. The GPU encodes
   sRGB when writing to the screen.
3. **Gizmos** (editor) and the **HUD** are drawn on top in display colours: they are not lit,
   exposed or tonemapped, so a white HUD label is always white.

The renderer measures GPU time per pass (`Shadows`, `Scene`, `Tonemap`, `Gizmos`, `HUD`): in
`FrameStats::passes`, and in noor_player's `--report` as `gpu_passes`. Showing them in the editor
is part 7 (the passes inspector).

## Units

| | Unit | Default | Where it's set |
|---|---|---|---|
| Sun | lux (illuminance) | 100,000 (bright sunshine) | `light` component: `illuminance` |
| Sun colour | linear RGB from `#RRGGBB` | `#FFF5E6` (slightly warm) | `light`: `color` |
| Sky | nits (cd/m²) | 8,000, light blue | `light`: `sky_luminance`, `sky_color` (until environments, part 5) |
| Exposure | EV100 | 14 | `camera`: `ev100` |

Exposure multiplies the scene by `1 / (1.2 × 2^EV100)`. Higher EV is darker. Handy values:
sunny day 14–15, overcast 12–13, sunset 11–12, bright interior 8–9, night street 3–5. One EV
step is twice or half the light, like one stop on a camera.

```json
"light":  { "type": "directional", "color": "#FFF5E6", "illuminance": 100000,
            "sky_color": "#6B9EFF", "sky_luminance": 8000 }
"camera": { "fov": 70, "near": 0.05, "ev100": 14, "tonemapper": "neutral" }
```

The sun shines along its entity's **forward** (−Z): rotate the entity to move the sun.
**Create ▸ Sun (directional light)** makes one. A scene without a sun gets the defaults above.

## Sun shadows

The sun casts shadows (`"shadows": true` on its `light`, the default) up to `shadow_distance`
metres from the camera (default 80). That distance is cut into **four cascades**: the near one is
short, so shadows close to you are sharp; the far ones are long and coarser. Each cascade is a
2048×2048 shadow map (`RendererDesc::shadow_map_size`).

- **Stable**: each cascade has a fixed size and moves in whole shadow-map texels, so shadow edges
  don't crawl or shimmer when the camera moves or turns (unit-tested).
- **Soft edges**: 3×3 hardware-filtered PCF, about two texels wide. Shadows fade out over the last
  10% of the shadow distance instead of stopping at a line.
- **Against "acne"** (surfaces shadowing themselves): a slope-scaled depth bias when drawing the
  shadow maps, and the looked-up point is pushed 1.5 texels out along the surface normal.
- Everything casts shadows except editor helpers (`DrawItem::cast_shadow`). Cutout materials cast
  cut-out shadows (a leaf throws a leaf shape).

```json
"light": { "type": "directional", "illuminance": 100000, "shadows": true, "shadow_distance": 80 }
```

## Point and spot lights

```json
"light": { "type": "point", "color": "#FFD9A0", "intensity": 800, "range": 10 }
"light": { "type": "spot",  "color": "#FFF0D8", "intensity": 3000, "range": 15, "angle": 30, "inner_angle": 22 }
```

- **`intensity` is in lumens**, the number on a bulb's box: a 60 W-equivalent bulb is about 800 lm,
  a bright LED floodlight 3000–5000 lm. A point light spreads them in all directions; a spot puts
  them into its cone, like a torch with a reflector (a narrower spot of the same lumens is
  brighter).
- Light fades with the square of the distance and reaches zero smoothly at `range` metres.
- A spot shines along its entity's forward (−Z); `angle` is the cone's half-angle in degrees,
  full brightness inside `inner_angle` (default ¾ of `angle`), fading to the edge.
- **Create ▸ Point light / Spot light** in the editor; the viewport marks them with a small marker
  in their colour.
- In daylight (EV 14) a lamp is invisible, as in a photo; it shows indoors or at night
  (`camera.ev100` 6–9). Up to 16 lights per frame are used (the ones nearest the camera).
- **Not yet**: shadows from point and spot lights; hundreds of lights (clustered lighting).

## The BRDF

Cook-Torrance microfacet specular plus Lambert diffuse, as in the glTF 2.0 metallic-roughness
model (the same maths as Filament, Unreal and Godot, so assets look alike across them):

- **Distribution**: GGX (Trowbridge-Reitz), `alpha = roughness²` (roughness is perceptual, 0..1).
- **Visibility**: height-correlated Smith-GGX.
- **Fresnel**: Schlick, `F0 = 0.04` for non-metals, the base colour for metals.
- **Diffuse**: Lambert, `base_color / π`, reduced by what the Fresnel term reflected and by
  metalness.
- **Ambient** (until image-based lighting): a hemisphere from the ground colour up to the sky
  colour. Diffuse uses the surface normal; specular uses the reflection direction, weighted by an
  analytic fit of the split-sum environment BRDF (Karis 2014), so every surface reflects a little
  sky, like real surfaces do.

Not yet: multi-scattering energy compensation (rough metals are slightly too dark), clear coat,
sheen, transmission.

## Tonemappers

| `tonemapper` | What it does |
|---|---|
| `neutral` (default) | **Khronos PBR Neutral**: colours up to ~0.76 stay almost exactly as authored; brighter ones roll off smoothly. Best for judging materials |
| `aces` | The common fit of the ACES film curve (Narkowicz 2015): more contrast, very bright colours drift towards white/yellow |
| `none` | Clamp: for checking exposure |

AgX (Blender's default) was the first plan in ADR-012. It waits for a licence check of the curve
data, so it isn't in yet.

## Colours: what "linear" means here

Colours written as `#RRGGBB` are sRGB, as every colour picker shows them; the engine converts
them to linear before lighting. All lighting maths is linear. Light colours are multiplied by
their intensity in physical units. Base colours are reflectances (0..1): real materials sit
between about 0.04 (charcoal) and 0.9 (fresh snow). Very dark base colours will look very dark
in sunlight. That's correct, but it surprises people used to unlit games.

## Tests

`ctest -L render`: `render.golden_Daylight` and `render.golden_Sunset` draw
`tests/data/render_scenes` and compare them with `tests/golden/*.png` (320×180, at most 1% of
pixels may differ by more than 12/255). `render.golden_catches_a_change` proves the comparison
fails on a different picture. To accept an intended change, look at the new picture first, then
run the test with `NOOR_UPDATE_GOLDEN=1`.
