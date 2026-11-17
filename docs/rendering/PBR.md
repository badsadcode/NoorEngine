# Lighting and shading (PBR)

How NoorEngine turns light into pixels (ADR-012). Status: **Milestone 5, part 1**. Surfaces don't
have materials yet (part 3), shadows come in part 4, skies from HDRIs in part 5.

## The frame

```
scene pass  ── lit draws ──▶ HDR image (RGBA 16-bit float, already exposed)
screen pass ── tonemap ─────▶ the screen (sRGB)  ── editor gizmos ── HUD
```

1. **Scene**: every mesh is lit by the sun and the sky in physical units and multiplied by the
   camera's exposure, into a 16-bit float image. Where nothing is drawn, the sky colour.
2. **Tonemap**: one full-screen triangle maps the HDR image to display colours. The GPU encodes
   sRGB when writing to the screen.
3. **Gizmos** (editor) and the **HUD** are drawn on top in display colours: they are not lit,
   exposed or tonemapped, so a white HUD label is always white.

The renderer measures GPU time per pass (`Scene`, `Tonemap`, `Gizmos`, `HUD`): in
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
