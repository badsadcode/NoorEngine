// Terrain sculpting (Milestone 6, part 6, ADR-031): brushes write a sparse layer of offsets over the
// generated base; the layer survives regenerating the base and round-trips through its file.
#include "noor/foundation/guid.h"
#include "noor/terrain/terrain.h"
#include "noor/terrain_edit/brush.h"

#include <ostream>
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace noor;
using terrain::Heights;
using terrain::SculptLayer;
using terrain_edit::Brush;
using terrain_edit::Tool;

namespace {

terrain::Settings flat_settings(float height = 5.0f) {
    terrain::Settings s;
    s.tiles_x = 2;
    s.tiles_z = 2;
    s.base = terrain::Settings::Base::Flat;
    s.flat_height = height;
    return s;
}

terrain::Settings noise_settings(std::uint64_t seed) {
    terrain::Settings s = flat_settings();
    s.base = terrain::Settings::Base::Noise;
    s.seed = seed;
    s.height = 30;
    s.scale = 120;
    return s;
}

// Applies dabs and recomposes, like the editor does.
void dab(const Heights& base, Heights& current, SculptLayer& layer, const Brush& brush, math::Vec2 at, float seconds) {
    if (auto rect = terrain_edit::apply(current, layer, brush, at, seconds)) current.recompose(base, layer, *rect);
}

std::filesystem::path temp_dir() {
    auto dir = std::filesystem::temp_directory_path() / ("noor_sculpt_" + Guid::generate().to_string());
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("sculpt: the brush weight is full inside, eases to nothing at the radius") {
    Brush b;
    b.radius = 10;
    b.falloff = 0.5f;
    CHECK(terrain_edit::brush_weight(b, 0) == 1.0f);
    CHECK(terrain_edit::brush_weight(b, 5) == 1.0f);
    CHECK(terrain_edit::brush_weight(b, 7.5f) == doctest::Approx(0.5f));
    CHECK(terrain_edit::brush_weight(b, 10) == 0.0f);
    CHECK(terrain_edit::brush_weight(b, 9.9f) < 0.01f);
    b.falloff = 0;
    CHECK(terrain_edit::brush_weight(b, 9.9f) == 1.0f);  // a hard edge
    CHECK(terrain_edit::tool_from_name("flatten") == Tool::Flatten);
    CHECK_FALSE(terrain_edit::tool_from_name("melt"));
}

TEST_CASE("sculpt: raise and lower change the layer, never the base") {
    auto base = Heights::build(flat_settings());
    REQUIRE(base);
    const auto base_hash = base->hash();
    Heights current = *base;
    SculptLayer layer(2, 2);
    Brush b;
    b.radius = 8;
    b.strength = 2;
    b.falloff = 0.5f;
    for (int i = 0; i < 10; ++i) dab(*base, current, layer, b, {10.0f, -20.0f}, 0.1f);  // one second
    CHECK(current.height(10, -20) == doctest::Approx(7.0f));   // 5 m + 2 m/s for 1 s at the centre
    CHECK(current.height(10, -14) > 5.0f);                      // inside the soft edge
    CHECK(current.height(10, -11) == doctest::Approx(5.0f));    // outside the radius
    CHECK(base->hash() == base_hash);
    CHECK(layer.tile_count() == 1);  // only the tile under the brush is stored
    b.tool = Tool::Lower;
    for (int i = 0; i < 5; ++i) dab(*base, current, layer, b, {10.0f, -20.0f}, 0.1f);
    CHECK(current.height(10, -20) == doctest::Approx(6.0f));
    // A brush off the terrain does nothing.
    CHECK_FALSE(terrain_edit::apply(current, layer, b, {1000.0f, 0.0f}, 1.0f));
}

TEST_CASE("sculpt: flatten levels towards a height, smooth evens out bumps") {
    auto base = Heights::build(noise_settings(3));
    REQUIRE(base);
    Heights current = *base;
    SculptLayer layer(2, 2);
    Brush b;
    b.tool = Tool::Flatten;
    b.radius = 12;
    b.strength = 20;
    b.falloff = 0.25f;
    b.flatten_height = 15;
    for (int i = 0; i < 40; ++i) dab(*base, current, layer, b, {0.0f, 0.0f}, 0.1f);
    for (float x = -6; x <= 6; x += 2)
        for (float z = -6; z <= 6; z += 2) CHECK(current.height(x, z) == doctest::Approx(15.0f).epsilon(1e-3));

    // Smooth: the roughness (how far samples are from their neighbours' average) goes down.
    auto rough = [&](const Heights& h) {
        double sum = 0;
        for (std::uint32_t z = 300; z < 340; ++z)
            for (std::uint32_t x = 300; x < 340; ++x)
                sum += std::fabs(h.at(x, z) - 0.25 * (h.at(x - 1, z) + h.at(x + 1, z) + h.at(x, z - 1) + h.at(x, z + 1)));
        return sum;
    };
    // Small sharp bumps first (what smoothing is for), then smooth them away.
    Brush bump;
    bump.radius = 1.5f;
    bump.strength = 3;
    bump.falloff = 0;
    for (float x = 46; x < 84; x += 5)
        for (float z = 46; z < 84; z += 5) dab(*base, current, layer, bump, {x, z}, 0.2f);
    const double before = rough(current);
    b.tool = Tool::Smooth;
    b.strength = 2;
    b.radius = 40;
    for (int i = 0; i < 20; ++i) dab(*base, current, layer, b, {64.0f, 64.0f}, 0.1f);
    CHECK(rough(current) < before * 0.5);
}

TEST_CASE("sculpt: a hill on a tile edge stays seamless, and survives regenerating the base") {
    auto base = Heights::build(noise_settings(5));
    REQUIRE(base);
    Heights current = *base;
    SculptLayer layer(2, 2);
    Brush b;
    b.radius = 10;
    b.strength = 4;
    for (int i = 0; i < 10; ++i) dab(*base, current, layer, b, {0.0f, 0.0f}, 0.1f);  // the corner where all four tiles meet
    CHECK(layer.tile_count() == 4);
    const auto t00 = terrain::tile_mesh(current, 0, 0), t10 = terrain::tile_mesh(current, 1, 0);
    for (std::uint32_t z = 240; z < terrain::kTileSamples; ++z) {
        CHECK(t00.vertices[z * terrain::kTileSamples + 256].position.y == t10.vertices[z * terrain::kTileSamples].position.y);
        CHECK(t00.vertices[z * terrain::kTileSamples + 256].normal.x == t10.vertices[z * terrain::kTileSamples].normal.x);
    }
    CHECK(current.at(256, 256) == doctest::Approx(base->at(256, 256) + 4.0f));

    // A new seed: different hills, the same hand-made bump on top of them.
    auto reseeded = Heights::build(noise_settings(6));
    REQUIRE(reseeded);
    const Heights recomposed = Heights::compose(*reseeded, layer);
    CHECK(reseeded->hash() != base->hash());
    for (std::uint32_t x = 240; x <= 272; x += 4)
        CHECK(recomposed.at(x, 256) - reseeded->at(x, 256) == doctest::Approx(current.at(x, 256) - base->at(x, 256)));
}

TEST_CASE("sculpt: the layer file round-trips, and settings with a sculpt file build base + layer") {
    SculptLayer layer(2, 2);
    layer.set(10, 20, 1.5f);
    layer.set(256, 100, -2.0f);  // on the edge between tiles (0, 0) and (1, 0)
    CHECK(layer.tile_count() == 2);
    CHECK(layer.offset(256, 100) == -2.0f);
    const auto dir = temp_dir();
    const auto file = dir / "hill.noorsculpt";
    REQUIRE(layer.save(file));
    auto back = SculptLayer::load(file);
    REQUIRE(back);
    CHECK(back->hash() == layer.hash());
    CHECK(back->offset(10, 20) == 1.5f);
    CHECK(back->offset(255, 100) == 0.0f);

    auto settings = flat_settings(3);
    settings.sculpt = file;
    auto built = Heights::build(settings);
    REQUIRE(built);
    CHECK(built->at(10, 20) == doctest::Approx(4.5f));
    CHECK(built->at(256, 100) == doctest::Approx(1.0f));
    CHECK(settings.key() != settings.base_only().key());

    // A damaged file is an error, not a silently flat terrain.
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << "NSCULPT1 and then nonsense";
    }
    CHECK_FALSE(SculptLayer::load(file));
    CHECK_FALSE(Heights::build(settings));
    std::filesystem::remove_all(dir);
}

TEST_CASE("sculpt: a ray finds the ground under the pointer") {
    auto h = Heights::build(noise_settings(9));
    REQUIRE(h);
    // Straight down from above.
    auto t = terrain::raycast(*h, {12.0f, 100.0f, -30.0f}, {0, -1, 0});
    REQUIRE(t);
    CHECK(100.0f - *t == doctest::Approx(h->height(12.0f, -30.0f)).epsilon(1e-3));
    // A slanted ray lands on the surface.
    const math::Vec3 o{-200.0f, 80.0f, -200.0f};
    const math::Vec3 d = math::normalize(math::Vec3{1.0f, -0.4f, 0.8f});
    auto s = terrain::raycast(*h, o, d);
    REQUIRE(s);
    const math::Vec3 p = o + d * *s;
    CHECK(p.y == doctest::Approx(h->height(p.x, p.z)).epsilon(1e-3));
    // Rays that miss: pointing up, or outside the terrain.
    CHECK_FALSE(terrain::raycast(*h, {0, 100, 0}, {0, 1, 0}));
    CHECK_FALSE(terrain::raycast(*h, {1000, 100, 0}, {0, -1, 0}));
}
