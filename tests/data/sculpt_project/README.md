# Sculpt Test

Test data for Milestone 6, part 6 (`tests/sculpt_check.cmake`): a flat terrain tile (256 m, 1 m
cells, 0 m high) with a hand-sculpted hill in `Assets/Terrain/Hill.noorsculpt`, built by
`noor_cli build`. The hill is a smooth bump, 6 m tall and 20 m in radius, centred on the terrain:
height = 6 x (cos(pi x d / 20) + 1) / 2 for d < 20 m. A character walks from x = -30 over the top;
the test checks its feet are on the hill (so the sculpt layer was packed, loaded and collided with).
