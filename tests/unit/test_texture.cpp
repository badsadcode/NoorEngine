// Textures (ADR-029): name guessing, colour spaces, linear-light mipmaps, normal maps, HDR,
// resizing, the cache and the registry.
#include "noor/asset/project.h"
#include "noor/asset/registry.h"
#include "noor/asset/texture.h"
#include "noor/foundation/guid.h"

#include <ostream>
#include <doctest/doctest.h>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace noor;
using asset::TextureKind;
using asset::TextureSettings;

namespace {

// A PNG in memory from RGBA bytes.
std::vector<std::uint8_t> png(int w, int h, const std::vector<std::uint8_t>& rgba) {
    std::vector<std::uint8_t> out;
    stbi_write_png_to_func([](void* ctx, void* data, int size) {
        auto* v = static_cast<std::vector<std::uint8_t>*>(ctx);
        v->insert(v->end(), static_cast<std::uint8_t*>(data), static_cast<std::uint8_t*>(data) + size);
    }, &out, w, h, 4, rgba.data(), w * 4);
    return out;
}

TextureSettings kind(TextureKind k, bool directx = false) {
    TextureSettings s;
    s.kind = k;
    s.directx_normals = directx;
    return s;
}

float half_to_float(std::uint16_t h) {
    const std::uint32_t sign = (h & 0x8000u) << 16, exp = (h >> 10) & 0x1F, mant = h & 0x3FF;
    if (exp == 0) return std::bit_cast<float>(sign) + (mant / 1024.0f) * std::pow(2.0f, -14.0f) * (sign ? -1.0f : 1.0f);
    return std::bit_cast<float>(sign | ((exp + 112) << 23) | (mant << 13));
}

} // namespace

TEST_CASE("texture: settings are guessed from the file name") {
    using asset::guess_texture_settings;
    CHECK(guess_texture_settings("Assets/wood_albedo.png").kind == TextureKind::Color);
    CHECK(guess_texture_settings("Assets/Brick-BaseColor.jpg").kind == TextureKind::Color);
    CHECK(guess_texture_settings("Assets/bricks_normal.png").kind == TextureKind::Normal);
    CHECK_FALSE(guess_texture_settings("Assets/bricks_normal.png").directx_normals);
    CHECK(guess_texture_settings("Assets/Rock_Normal_DX.png").directx_normals);
    CHECK(guess_texture_settings("Assets/rock_nor_gl_1k.png").kind == TextureKind::Normal);
    for (const char* data : {"x_roughness.png", "x_rough.png", "x_metallic.png", "x_ao.png", "x_orm.png", "x_height.png",
                             "x_depth.png", "x_curvature.png", "x_cavity.png", "x_mask.tga"})
        CHECK_MESSAGE(guess_texture_settings(data).kind == TextureKind::Data, data);
    CHECK(guess_texture_settings("Assets/sky.hdr").kind == TextureKind::Hdr);
    // Round trip through the .noormeta form.
    const TextureSettings s = guess_texture_settings("a_normal_dx.png");
    CHECK(TextureSettings::from_json(s.to_json()) == s);
    CHECK(asset::is_texture_file("A.PNG"));
    CHECK_FALSE(asset::is_texture_file("a.psd"));
}

TEST_CASE("texture: colour is averaged in linear light, data as stored") {
    // A 2x2 black-and-white checker: the 1x1 mip is half the light. In sRGB that's 188, not 128.
    const auto checker = png(2, 2, {0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255});
    auto color = asset::import_texture_memory(checker, kind(TextureKind::Color), "checker");
    auto data = asset::import_texture_memory(checker, kind(TextureKind::Data), "checker");
    REQUIRE(color);
    REQUIRE(data);
    CHECK(color->format == asset::TextureFormat::Rgba8Srgb);
    CHECK(data->format == asset::TextureFormat::Rgba8Unorm);
    REQUIRE(color->mips.size() == 2);
    CHECK(color->mips[0].bytes[0] == 0);
    CHECK(color->mips[0].bytes[4] == 255);  // the full-size mip keeps the source values
    CHECK(color->mips[1].bytes[0] == doctest::Approx(188).epsilon(0.01));
    CHECK(data->mips[1].bytes[0] == doctest::Approx(128).epsilon(0.01));
}

TEST_CASE("texture: mip chains halve down to 1x1, max_size shrinks first") {
    std::vector<std::uint8_t> px(5 * 3 * 4, 200);
    auto t = asset::import_texture_memory(png(5, 3, px), kind(TextureKind::Color), "odd");
    REQUIRE(t);
    REQUIRE(t->mips.size() == 3);
    CHECK((t->mips[0].width == 5 && t->mips[0].height == 3));
    CHECK((t->mips[1].width == 2 && t->mips[1].height == 1));
    CHECK((t->mips[2].width == 1 && t->mips[2].height == 1));
    CHECK(t->gpu_bytes() == (15 + 2 + 1) * 4);

    TextureSettings small = kind(TextureKind::Color);
    small.max_size = 4;
    small.mips = false;
    auto big = asset::import_texture_memory(png(16, 8, std::vector<std::uint8_t>(16 * 8 * 4, 90)), small, "big");
    REQUIRE(big);
    CHECK(big->mips.size() == 1);
    CHECK((big->width() == 4 && big->height() == 2));
    CHECK((big->source_width == 16 && big->source_height == 8));
}

TEST_CASE("texture: normal maps stay unit length in mips, DirectX green is flipped") {
    // Two normals tilted +X and -X: their average points straight out (+Z) once renormalised.
    const auto n = png(2, 1, {218, 128, 218, 255, 38, 128, 218, 255});
    auto t = asset::import_texture_memory(n, kind(TextureKind::Normal), "tilts");
    REQUIRE(t);
    CHECK(t->format == asset::TextureFormat::Rgba8Unorm);
    REQUIRE(t->mips.size() == 2);
    const auto& m = t->mips[1].bytes;
    CHECK(m[0] == doctest::Approx(128).epsilon(0.02));
    CHECK(m[1] == doctest::Approx(128).epsilon(0.02));
    CHECK(m[2] == 255);  // renormalised to +Z, not the shorter plain average

    const auto up = png(1, 1, {128, 200, 230, 255});
    auto gl = asset::import_texture_memory(up, kind(TextureKind::Normal), "gl");
    auto dx = asset::import_texture_memory(up, kind(TextureKind::Normal, true), "dx");
    REQUIRE(gl);
    REQUIRE(dx);
    CHECK(gl->mips[0].bytes[1] == 200);
    CHECK(dx->mips[0].bytes[1] == 55);  // 255 - 200
}

TEST_CASE("texture: .hdr images keep light values above 1 as 16-bit floats") {
    const auto dir = std::filesystem::temp_directory_path() / ("noor_tex_" + Guid::generate().to_string());
    std::filesystem::create_directories(dir);
    const float pixel[3] = {4.0f, 0.5f, 0.25f};
    REQUIRE(stbi_write_hdr((dir / "sky.hdr").string().c_str(), 1, 1, 3, pixel));
    auto t = asset::import_texture(dir / "sky.hdr", asset::guess_texture_settings(dir / "sky.hdr"));
    REQUIRE(t);
    CHECK(t->format == asset::TextureFormat::Rgba16Float);
    std::uint16_t h[4];
    std::memcpy(h, t->mips[0].bytes.data(), 8);
    CHECK(half_to_float(h[0]) == doctest::Approx(4.0f).epsilon(0.01));
    CHECK(half_to_float(h[1]) == doctest::Approx(0.5f).epsilon(0.01));
    std::filesystem::remove_all(dir);
}

TEST_CASE("texture: broken files are errors, not crashes") {
    const std::vector<std::uint8_t> junk{1, 2, 3, 4, 5};
    CHECK_FALSE(asset::import_texture_memory(junk, kind(TextureKind::Color), "junk"));
    CHECK_FALSE(asset::import_texture_memory({}, kind(TextureKind::Color), "empty"));
}

TEST_CASE("texture: the cache is used on the second load, the registry imports textures with a guess") {
    const auto dir = std::filesystem::temp_directory_path() / ("noor_texproj_" + Guid::generate().to_string());
    std::filesystem::create_directories(dir / "Assets" / "Textures");
    const auto file = dir / "Assets" / "Textures" / "wall_normal.png";
    {
        const auto bytes = png(4, 4, std::vector<std::uint8_t>(64, 128));
        std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    std::ofstream(dir / "Test.noorproj") << R"({"noor_project": 1, "id": "11111111-2222-4333-8444-555555555555", "name": "Test", "engine": "0.0.1"})";
    auto project = asset::Project::open(dir);
    REQUIRE(project);
    asset::AssetRegistry registry;
    REQUIRE(registry.scan(*project));
    const auto* record = registry.find_path("Assets/Textures/wall_normal.png");
    REQUIRE(record);
    CHECK(record->importer == "texture");
    CHECK(asset::texture_settings_for(file).kind == TextureKind::Normal);  // the guess, from the .noormeta

    const auto cache = dir / ".noor" / "cache" / "textures";
    auto first = asset::load_texture(file, cache);
    REQUIRE(first);
    REQUIRE(std::filesystem::exists(cache));
    std::size_t files = 0;
    for ([[maybe_unused]] const auto& e : std::filesystem::directory_iterator(cache)) ++files;
    CHECK(files == 1);
    auto second = asset::load_texture(file, cache);
    REQUIRE(second);
    CHECK(second->mips.size() == first->mips.size());
    CHECK(second->mips[0].bytes == first->mips[0].bytes);
    std::filesystem::remove_all(dir);
}
