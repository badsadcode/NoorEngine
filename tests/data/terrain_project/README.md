# Terrain Test

Test data for Milestone 6, part 5 (`tests/terrain_check.cmake`): one terrain tile from a 16-bit ramp
heightmap (0 m at -X to 20 m at +X over 256 m), built by `noor_cli build`. A character walks up the
ramp; the test checks its feet stay on the ground (height = (x + 128) / 256 x 20).
