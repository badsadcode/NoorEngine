// Terrain (Milestone 6, part 5, ADR-031): deterministic generation, seams between tiles, the height
// lookup agreeing with the mesh, heightmap import, settings.
#include "noor/foundation/guid.h"
#include "noor/terrain/terrain.h"

#include <ostream>
#include <doctest/doctest.h>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <stb_image_write.h>  // the implementation is in test_texture.cpp
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <chrono>
#include <cmath>
#include <filesystem>
#include <random>

using namespace noor;
using terrain::Settings;

#define TERRAIN_SEED7_HASH 5336048917472943244ull  // measured 2026-11-18 (Debug and Release, MSVC x64)

namespace {

Settings hills(std::uint64_t seed = 7) {
    Settings s;
    s.tiles_x = 2;
    s.tiles_z = 2;
    s.base = Settings::Base::Noise;
    s.seed = seed;
    s.height = 40;
    s.scale = 250;
    s.octaves = 5;
    return s;
}

} // namespace

TEST_CASE("terrain: the same seed gives the same terrain, bit for bit") {
    auto a = terrain::Heights::build(hills());
    auto b = terrain::Heights::build(hills());
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->hash() == b->hash());
    CHECK(a->samples_x() == 513);
    // Pinned: if this changes, terrains made with this seed look different in existing projects. Change
    // it only on purpose (and say so in the changelog). Integer hashing and + - * / only, so it holds
    // on any machine.
    MESSAGE("seed 7 fingerprint: " << a->hash());
    CHECK(a->hash() == 0u + TERRAIN_SEED7_HASH);
    auto other = terrain::Heights::build(hills(8));
    REQUIRE(other);
    CHECK(other->hash() != a->hash());
    // Heights stay within 0..height, and actually use the range.
    float lo = 1e9f, hi = -1e9f;
    for (std::uint32_t z = 0; z < a->samples_z(); ++z)
        for (std::uint32_t x = 0; x < a->samples_x(); ++x) {
            lo = std::min(lo, a->at(x, z));
            hi = std::max(hi, a->at(x, z));
        }
    CHECK(lo >= 0.0f);
    CHECK(hi <= 40.0f);
    CHECK(hi - lo > 10.0f);
    for (int i = 0; i < 1000; ++i) {
        const float n = terrain::fractal_noise(i * 0.37, i * -0.21, 3, 4);
        CHECK(n >= -1.0f);
        CHECK(n <= 1.0f);
    }
}

TEST_CASE("terrain: neighbouring tiles meet exactly (positions and normals on the shared edge)") {
    auto h = terrain::Heights::build(hills());
    REQUIRE(h);
    const auto left = terrain::tile_mesh(*h, 0, 0), right = terrain::tile_mesh(*h, 1, 0), above = terrain::tile_mesh(*h, 0, 1);
    {
        // Cost (printed, not checked: timings vary by machine): heights for 2 x 2 tiles and one tile mesh.
        const auto t0 = std::chrono::steady_clock::now();
        auto again = terrain::Heights::build(hills(9));
        const auto t1 = std::chrono::steady_clock::now();
        const auto mesh = terrain::tile_mesh(*again, 0, 0);
        const auto t2 = std::chrono::steady_clock::now();
        MESSAGE("2 x 2 tiles: heights " << std::chrono::duration<double, std::milli>(t1 - t0).count() << " ms ("
                << again->samples_x() * again->samples_z() * sizeof(float) << " bytes); one tile mesh "
                << std::chrono::duration<double, std::milli>(t2 - t1).count() << " ms ("
                << mesh.vertices.size() * sizeof(mesh.vertices[0]) + mesh.indices.size() * sizeof(mesh.indices[0]) << " bytes)");
    }
    CHECK(left.vertices.size() == 257u * 257u);
    CHECK(left.indices.size() == 256u * 256u * 6u);
    for (std::uint32_t z = 0; z < terrain::kTileSamples; ++z) {
        const auto& a = left.vertices[z * terrain::kTileSamples + 256];
        const auto& b = right.vertices[z * terrain::kTileSamples + 0];
        CHECK(a.position.x == b.position.x);
        CHECK(a.position.y == b.position.y);
        CHECK(a.position.z == b.position.z);
        CHECK(a.normal.x == b.normal.x);
        CHECK(a.normal.y == b.normal.y);
    }
    for (std::uint32_t x = 0; x < terrain::kTileSamples; ++x) {
        const auto& a = left.vertices[256 * terrain::kTileSamples + x];
        const auto& b = above.vertices[x];
        CHECK(a.position.y == b.position.y);
        CHECK(a.normal.z == b.normal.z);
    }
    // Triangles face up (counter-clockwise seen from above).
    const auto& v = left.vertices;
    const auto& i = left.indices;
    const math::Vec3 n = math::cross(v[i[1]].position - v[i[0]].position, v[i[2]].position - v[i[0]].position);
    CHECK(n.y > 0.0f);
    // The terrain is centred on its entity.
    CHECK(left.vertices[0].position.x == doctest::Approx(-256.0f));
    CHECK(right.vertices.back().position.z == doctest::Approx(0.0f));
}

TEST_CASE("terrain: the height lookup lands exactly on the mesh triangles") {
    auto h = terrain::Heights::build(hills());
    REQUIRE(h);
    const auto mesh = terrain::tile_mesh(*h, 0, 0);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> cell(0.0f, 255.999f);
    for (int k = 0; k < 200; ++k) {
        const float u = cell(rng), w = cell(rng);
        const auto x0 = static_cast<std::uint32_t>(u), z0 = static_cast<std::uint32_t>(w);
        const float fx = u - x0, fz = w - z0;
        // The mesh's triangle under the point: the cell's diagonal runs from (x0, z0) to (x0+1, z0+1).
        const auto p = [&](std::uint32_t x, std::uint32_t z) { return mesh.vertices[z * terrain::kTileSamples + x].position; };
        const math::Vec3 a = p(x0, z0), b = p(x0 + 1, z0), c = p(x0, z0 + 1), d = p(x0 + 1, z0 + 1);
        const math::Vec3 q0 = a, q1 = fz >= fx ? c : d, q2 = fz >= fx ? d : b;
        const math::Vec3 n = math::cross(q1 - q0, q2 - q0);
        const float px = a.x + fx, pz = a.z + fz;
        const float expected = q0.y - (n.x * (px - q0.x) + n.z * (pz - q0.z)) / n.y;  // the plane through the triangle
        CHECK(h->height(px, pz) == doctest::Approx(expected).epsilon(1e-4));
    }
}

TEST_CASE("terrain: a heightmap picture becomes heights between min and max, settings come from the component") {
    // A 16 x 16 picture: black on the left, white on the right (8-bit here; 16-bit PNGs keep more steps).
    std::vector<std::uint8_t> px(16 * 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) px[y * 16 + x] = static_cast<std::uint8_t>(x * 17);
    const auto dir = std::filesystem::temp_directory_path() / ("noor_terrain_" + Guid::generate().to_string());
    std::filesystem::create_directories(dir);
    REQUIRE(stbi_write_png((dir / "ramp.png").string().c_str(), 16, 16, 1, px.data(), 16));
    const Guid map = Guid::generate();
    const asset::Json component{{"tiles", {1, 1}}, {"base", "heightmap"}, {"heightmap", map.to_string()}, {"min_height", 2}, {"max_height", 12}};
    const auto settings = Settings::from_json(component, [&](const Guid& id) -> std::optional<std::filesystem::path> {
        return id == map ? std::optional(dir / "ramp.png") : std::nullopt;
    });
    CHECK(settings.base == Settings::Base::Heightmap);
    CHECK((settings.tiles_x == 1 && settings.tiles_z == 1));
    auto h = terrain::Heights::build(settings);
    REQUIRE(h);
    CHECK(h->at(0, 100) == doctest::Approx(2.0f));    // left edge: black
    CHECK(h->at(256, 100) == doctest::Approx(12.0f));  // right edge: white
    CHECK(h->height(0.0f, 0.0f) == doctest::Approx(7.0f).epsilon(0.02));  // the middle: half way
    CHECK(h->normal(128, 128).x < 0.0f);  // rising towards +x: the normal leans back
    // Shared heights: the same settings give the same object.
    auto s1 = terrain::Heights::shared(settings);
    auto s2 = terrain::Heights::shared(settings);
    REQUIRE(s1);
    REQUIRE(s2);
    CHECK(s1->get() == s2->get());
    // A missing heightmap is an error, not a flat surprise.
    auto missing = settings;
    missing.heightmap.clear();
    CHECK_FALSE(terrain::Heights::build(missing));
    std::filesystem::remove_all(dir);

    // Defaults and a flat terrain.
    const auto flat = Settings::from_json({{"base", "flat"}, {"flat_height", 3}}, {});
    auto fh = terrain::Heights::build(flat);
    REQUIRE(fh);
    CHECK(fh->height(10, -20) == doctest::Approx(3.0f));
    CHECK(Settings::from_json(asset::Json::object(), {}).base == Settings::Base::Noise);
}
