# Render Scenes (test data)

The golden-image render tests (`tests/render_golden.cmake`, Milestone 5): each scene is drawn by
noor_player and compared with `tests/golden/<scene>.png` (320x180) by `noor_image_compare`.

- `Daylight`: the default sun and sky, EV 14, Khronos PBR Neutral.
- `Sunset`: a warm low sun (20,000 lux from the right) under a dim sky (1,200 nits), EV 12, the
  ACES fit.

The objects span the albedo range: a near-black box (#141414), greys, a near-white ball
(#F2F2F2) and saturated red, green and blue, so exposure, tonemapping or colour-space mistakes
show up. When a change is meant to alter the picture: run the test, look at the screenshot and
the diff it leaves in the build folder (`render_golden_<scene>/`), and if it's right, run the
test again with the environment variable `NOOR_UPDATE_GOLDEN=1` to replace the reference.
