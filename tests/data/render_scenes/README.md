# Render Scenes (test data)

The golden-image render tests (`tests/render_golden.cmake`, Milestone 5): each scene is drawn by
noor_player and compared with `tests/golden/<scene>.png` (320x180) by `noor_image_compare`.

- `Daylight`: the default sun and sky, EV 14, Khronos PBR Neutral.
- `Textured`: a checker texture on the floor (repeated 15x) and a box, a brick normal map on a
  wall and a ball, a shiny metal ball; the sun from the left (Milestone 5, part 2).
- `Materials`: `.noormat` materials (brick, an instance of it, a glowing one), a tiles texture set
  turned into a material by `noor_cli make-material` (albedo, normal, ORM) and an instance repeating
  it on the floor, and a GLB with three materials (embedded texture, gold metal, cutout leaf)
  (Milestone 5, part 3).
- `Sunset`: a warm low sun (20,000 lux from the right) under a dim sky (1,200 nits), EV 12, the
  ACES fit.
- `Atmosphere_Noon`, `Atmosphere_Sunset`: the same objects under the physically based sky (Milestone 8A),
  the sun at 60 and 3 degrees; rendered by the GPU path and, with `--cpu-sky`, by the CPU reference.
- `Atmosphere_Clouds`, `Atmosphere_Overcast`: two cloud layers (cumulus and a high veil) late in the afternoon,
  and a full overcast deck (Milestone 8A, part 4); `tests/clouds_cost_check.cmake` measures what clouds cost.
- `Space`: the objects under a black sky with a ringed planet, a small moon, a white sun and a warm second sun
  (Milestone 8A, part 5; the stars are raised for the look).
- `Atmosphere_Day`: `Scripts/DayCycle.luau` drags the time of day from 5:00 to 21:00 over 8 seconds
  (`tests/day_cycle_check.cmake` measures the moving sun).

The objects span the albedo range: a near-black box (#141414), greys, a near-white ball
(#F2F2F2) and saturated red, green and blue, so exposure, tonemapping or colour-space mistakes
show up. When a change is meant to alter the picture: run the test, look at the screenshot and
the diff it leaves in the build folder (`render_golden_<scene>/`), and if it's right, run the
test again with the environment variable `NOOR_UPDATE_GOLDEN=1` to replace the reference.
