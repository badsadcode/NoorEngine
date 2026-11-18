# Lighting and shading (PBR)

How NoorEngine turns light into pixels (ADR-012). Status: **Milestone 5, parts 1–5** (frame, sun,
materials in [MATERIALS.md](MATERIALS.md), shadows, point and spot lights, environments).

## The frame

```
shadow pass ── casters from the sun ──▶ 4 shadow cascades (depth)
scene pass  ── opaque, sky, see-through ▶ HDR image (RGBA 16-bit float, exposed; MSAA resolved)
screen pass ── tonemap ───────────────▶ the screen (sRGB)  ── editor gizmos ── HUD
```

0. **Shadows**: everything that casts a shadow is drawn from the sun into four shadow maps (see
   below).
1. **Scene**: every mesh is lit by the sun and the sky in physical units and multiplied by the
   camera's exposure, into a 16-bit float image. Opaque things first, then the sky where nothing
   was drawn, then **see-through** things (below). With anti-aliasing on, this pass draws into a
   multisampled image that is averaged into the HDR image at its end.
2. **Tonemap**: one full-screen triangle maps the HDR image to display colours. The GPU encodes
   sRGB when writing to the screen.
3. **Gizmos** (editor) and the **HUD** are drawn on top in display colours: they are not lit,
   exposed or tonemapped, so a white HUD label is always white.

The renderer measures GPU time per pass (`Shadows`, `Scene`, `Tonemap`, `Gizmos`, `HUD`): in
`FrameStats::passes`, in noor_player's `--report` (`gpu_passes`, and the `render` section with
draws, culled shadow casters, memory and MSAA samples), and in the editor's **Window ▸ Render
stats** panel. Measured numbers and the decisions taken from them: [MEASUREMENTS.md](MEASUREMENTS.md).

### Anti-aliasing (MSAA)

The scene is drawn with **4 samples per pixel** by default (`RendererDesc::msaa_samples`: 1, 2, 4
or 8, lowered to what the GPU supports; `Renderer::set_msaa` changes it at run time). Edges get
smooth; textures and shading are computed once per pixel as before. The samples are averaged
before tonemapping, so very bright edges against dark ones stay a little crisp (a known
trade-off of resolving HDR; a tonemapped resolve can come later if it shows). Editor: Preferences
▸ Anti-aliasing. Player: `--msaa N`. 4× costs about 42 MB more at 1280×720.

### See-through surfaces

Materials with `"alpha": "blend"` are drawn after the opaque scene and the sky, **back to front**
(by the distance of each object's bounding-sphere centre), with depth testing but no depth
writes. Blending is premultiplied: the light a surface sends back is scaled by its alpha, while
reflections and glow keep their full strength (a clear window still reflects the sky). This is
**alpha blending, not tinted glass**: the light behind is mixed with the surface's colour, not
filtered by it. Sorting is per object, so objects inside each other or long intersecting panes
can sort wrongly. See-through surfaces cast shadows like cutouts (below `alpha_cutoff`: none).

## Units

| | Unit | Default | Where it's set |
|---|---|---|---|
| Sun | lux (illuminance) | 100,000 (bright sunshine) | `light` component: `illuminance` |
| Sun colour | linear RGB from `#RRGGBB` | `#FFF5E6` (slightly warm) | `light`: `color` |
| Sky (procedural) | nits (cd/m²) | 8,000, light blue | `light`: `sky_luminance`, `sky_color` |
| Environment (HDRI) | nits per HDRI unit | 8,000 | `environment`: `intensity` |
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

## Environments (image-based lighting)

Everything around the scene lights it: the sky, the ground, distant buildings. NoorEngine takes
that from an **environment**: an HDRI photo, or a procedural sky when the scene has none.

```json
"environment": { "hdri": "<.hdr texture id>", "intensity": 8000, "rotation": 0, "background": true }
```

- **`hdri`**: an equirectangular `.hdr` (Poly Haven's are CC0). In the editor, **Add to scene** on
  an `.hdr` makes it the scene's environment.
- **`intensity`**: nits per HDRI unit. HDRI files aren't calibrated to real brightness, so this is
  the knob; 8000 suits most daylight HDRIs at EV 13–14.
- **`rotation`**: degrees about the vertical axis (turns the light and the background together).
- **`background`**: draw the HDRI as the sky (or keep the clear colour).
- **No environment**: a procedural sky from the sun's `sky_color` / `sky_luminance` (blue overhead,
  paler at the horizon, `ground_color` below). It lights the scene the same way, so metals have
  something to reflect.
- **The sun**: an HDRI already contains its sun, so a scene with an HDRI and no `light` entity gets
  no default sun. Add a directional light (pointing like the photo's sun) for sharp sun shadows.

### The atmosphere sky (Milestone 8A, [ADR-033](../decisions/ADR-033-environments-and-atmosphere.md))

```json
"environment": { "sky": "atmosphere", "turbidity": 1, "ground_albedo": 0.3, "altitude": 0 }
```

The sky is built from the sun: point the directional light and the sky follows (blue at noon, a warm
glow round a low sun, a blue zenith at dusk). The sun's light is coloured by the air it crosses (white
at noon, orange-red at sunset) and its disc is drawn in the sky.
- **`sky`**: `"atmosphere"`; `"hdri"` (the default with an HDRI) or `"gradient"` (the three-colour sky,
  even when an HDRI is set). The editor's **sky** dropdown switches it.
- **`turbidity`**: haze. 1 a clear day, 3–5 hazy, 10 thick haze (dims and yellows the sun, whitens the sky).
- **`ground_albedo`**: how much the planet's surface reflects (0–1); it brightens the sky a little and
  lights the scene from below.
- **`altitude`**: the observer's height in metres (a mountain top sees a darker sky).
- **The sun light** then means: `illuminance` **outside** the atmosphere (100,000 lux is a good value;
  at noon about 85% of it reaches the ground) and `color` the sun's own colour (leave it out: white). The
  sky's brightness follows the sun's, so exposure is the only other knob (EV 15 at noon, about 11 at sunset).
- Under the horizon the sky shows distant land seen through 20 km of haze.
- Today the sky is rebuilt **on the CPU** when the sun moves (about 120 ms); a moving sun (time of day)
  arrives with GPU tables in part 2.

How it works, once per environment, on the CPU, cached in `<project>/.noor/cache`:
- a **prefiltered cubemap** (faces up to 256², one mip per roughness step): mip 0 is the sharp
  environment, each next mip what a rougher surface reflects (GGX importance sampling with
  filtered lookups, Křivánek 2008);
- **irradiance as 9 spherical-harmonics coefficients** (Ramamoorthi & Hanrahan 2001): the soft
  light a matte surface gets, from nine colours;
- a **BRDF lookup table** (split sum, Karis 2013), computed once when the renderer starts.

In the shader: diffuse = albedo / π × irradiance(normal); specular = prefiltered(reflection,
roughness) × (F0 × A + B); both multiplied by the material's ambient occlusion.

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

**Culling per cascade**: a caster is drawn into a cascade only if its bounding sphere touches the
cascade's sides (`render::sphere_touches_cascade`). Casters far towards the sun still count.
`FrameStats::shadow_draws_culled` counts the skipped ones.

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
- **Ambient**: image-based lighting from the environment (above).

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

`ctest -L render`: `render.golden_*` (Daylight, Sunset, Textured, Materials, Lights, three HDRIs, Glass) draw
`tests/data/render_scenes` and compare them with `tests/golden/*.png` (320×180, at most 1% of
pixels may differ by more than 12/255). `render.golden_catches_a_change` proves the comparison
fails on a different picture. To accept an intended change, look at the new picture first, then
run the test with `NOOR_UPDATE_GOLDEN=1`.
