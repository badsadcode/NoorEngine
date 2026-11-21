#include "noor/game/sprites.h"
#include "noor/game/tilemap.h"

#include "noor/asset/texture.h"
#include "noor/foundation/log.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace noor::game {
namespace {

using asset::Json;
using math::Vec3;

float number(const Json& j, const char* key, float fallback) {
    return j.is_object() && j.contains(key) && j[key].is_number() ? j[key].get<float>() : fallback;
}

Vec3 linear_of(const Json& c) {  // "#RRGGBB" (sRGB) or [r, g, b] (linear)
    if (c.is_array() && c.size() >= 3 && c[0].is_number() && c[1].is_number() && c[2].is_number())
        return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>()};
    if (c.is_string()) {
        const std::string s = c.get<std::string>();
        if (s.size() == 7 && s[0] == '#') {
            const auto ch = [&](std::size_t at) {
                const float v = static_cast<float>(std::stoul(s.substr(at, 2), nullptr, 16)) / 255.0f;
                return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
            };
            try {
                return {ch(1), ch(3), ch(5)};
            } catch (const std::exception&) {
            }
        }
    }
    return {1, 1, 1};
}

std::uint32_t pack(Vec3 c) {
    const auto b = [](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return b(c.x) | (b(c.y) << 8) | (b(c.z) << 16);
}

void warn_once(const std::string& message) {
    static std::unordered_set<std::string> said;
    if (said.insert(message).second) NOOR_LOG_WARN("Sprites", "{}", message);
}

} // namespace

SpriteLibrary::SpriteLibrary(render::Renderer& renderer, TextureLibrary& textures, asset::AssetFileResolver resolve)
    : renderer_(renderer), textures_(textures), resolve_(std::move(resolve)) {
    // One quad for every sprite: (0,0) to (1,1) in its own XY plane, facing +Z (towards the camera once turned), its
    // normal tilted up so it takes the sky's light as well as what's in front of it (a flat card would be dark whenever
    // the sun is behind it).
    const Vec3 n = math::normalize(Vec3{0.0f, 0.5f, 0.866f});
    const render::Vertex v[4] = {{{0, 0, 0}, n, {1, 1, 1}, {0, 1}, {1, 0, 0, 1}},
                                 {{1, 0, 0}, n, {1, 1, 1}, {1, 1}, {1, 0, 0, 1}},
                                 {{1, 1, 0}, n, {1, 1, 1}, {1, 0}, {1, 0, 0, 1}},
                                 {{0, 1, 0}, n, {1, 1, 1}, {0, 0}, {1, 0, 0, 1}}};
    const std::uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
    if (auto m = renderer_.create_mesh({v, idx})) quad_ = *m;
}

SpriteLibrary::~SpriteLibrary() {
    for (auto& [file, s] : sheets_)
        for (auto& [key, m] : s.materials) renderer_.destroy_material(m);
    if (quad_.valid()) renderer_.destroy_mesh(quad_);
}

std::size_t SpriteLibrary::refresh() {
    std::size_t changed = 0;
    for (auto it = sheets_.begin(); it != sheets_.end();) {
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(it->first, ec);
        if (!ec && time == it->second.time) {
            ++it;
            continue;
        }
        for (auto& [key, m] : it->second.materials) renderer_.destroy_material(m);
        it = sheets_.erase(it);  // loads again when next drawn
        ++changed;
    }
    return changed;
}

SpriteLibrary::Sheet* SpriteLibrary::sheet(const std::filesystem::path& file) {
    const auto [it, fresh] = sheets_.try_emplace(file.generic_string());
    Sheet& s = it->second;
    if (!fresh) return s.ok ? &s : nullptr;  // loaded (or failed) before; refresh() notices changed files
    std::error_code ec;
    s.time = std::filesystem::last_write_time(file, ec);
    auto loaded = asset::load_sprite_file(file);
    if (!loaded) {
        warn_once(loaded.error().message);
        return nullptr;
    }
    const auto texture = loaded->texture && resolve_ ? resolve_(*loaded->texture) : std::nullopt;
    if (!texture) {
        warn_once(std::format("{}: its texture isn't in the project", file.filename().string()));
        return nullptr;
    }
    auto info = asset::texture_info(*texture, asset::TextureSettings{});
    if (!info) {
        warn_once(info.error().describe());
        return nullptr;
    }
    if (auto sliced = asset::slice(*loaded, static_cast<int>(info->source_width), static_cast<int>(info->source_height)); !sliced) {
        warn_once(std::format("{}: {}", file.filename().string(), sliced.error().message));
        return nullptr;
    }
    s.sheet = std::move(*loaded);
    s.texture = textures_.get(*texture);
    s.ok = true;
    return &s;
}

render::MaterialHandle SpriteLibrary::material(Sheet& s, int frame, Vec3 tint, bool flip, int look) {
    const auto key = std::make_tuple(frame, pack(tint), flip, look);
    if (const auto it = s.materials.find(key); it != s.materials.end()) return it->second;
    const auto& f = s.sheet.frames[static_cast<std::size_t>(frame)];
    render::MaterialDesc d;
    d.base_color = {tint.x, tint.y, tint.z, 1.0f};
    d.roughness = 0.9f;
    d.alpha = render::AlphaMode::Cutout;
    d.alpha_cutoff = 0.5f;
    d.double_sided = true;
    d.nearest_filter = s.sheet.nearest;
    d.clamp_uv = true;  // only its own frame, never a texel of the next one (2D part 3)
    if ((look & 1) != 0) {  // sorted or soft-edged: the see-through pass, its alpha covering all it shows
        d.alpha = render::AlphaMode::Blend;
        d.coverage = true;
    }
    d.unlit = (look & 2) != 0;
    d.uv_scale = f.uv_scale;
    d.uv_offset = f.uv_offset;
    if (flip) {  // mirrored in the texture, not by a negative scale (that would turn the quad's front face away)
        d.uv_scale.x = -f.uv_scale.x;
        d.uv_offset.x = f.uv_offset.x + f.uv_scale.x;
    }
    d.base_color_texture = s.texture;
    auto m = renderer_.create_material(d);
    const render::MaterialHandle handle = m ? *m : render::MaterialHandle{};
    s.materials.emplace(key, handle);
    return handle;
}

int SpriteLibrary::frame_of(const SpriteInstance& sprite, Vec3 eye, double seconds) {
    Sheet* s = sheet(sprite.sheet);
    if (!s || s->sheet.frames.empty()) return -1;
    return sprite_frame(s->sheet, sprite.component, sprite.world, eye, seconds);
}

void SpriteLibrary::add_draws(std::span<const SpriteInstance> sprites, Vec3 eye, double seconds, std::vector<render::DrawItem>& out,
                              const Vec3* parallel) {
    if (!quad_.valid()) return;
    for (const SpriteInstance& sp : sprites) {
        // A parallel view looks at every sprite from the same direction: an eye just behind it along that direction.
        if (parallel) eye = sp.world.transform_point({0, 0, 0}) - *parallel;
        const int frame = frame_of(sp, eye, seconds);
        if (frame < 0) continue;
        Sheet& s = *sheet(sp.sheet);
        const Json& c = sp.component;
        const math::Mat4& w = sp.world;
        Vec3 pos = w.transform_point({0, 0, 0});
        // "y_depth" (2D part 7): the isometric map's depth rule by the pivot's height, so walls in front hide the sprite
        // and it hides walls behind, pixel by pixel (hard-edged sprites only: those write depth).
        if (c.is_object() && c.value("y_depth", false)) pos.z += standing_depth(pos.y);
        const Vec3 scale{math::length(Vec3{w.m[0].x, w.m[0].y, w.m[0].z}), math::length(Vec3{w.m[1].x, w.m[1].y, w.m[1].z}), 1.0f};
        // Turned to the camera: +Z of the quad towards the eye (upright: about the vertical only).
        math::Mat4 turn = math::Mat4::identity();
        const std::string mode = c.is_object() ? c.value("billboard", std::string("upright")) : std::string("upright");
        const Vec3 to_eye = eye - pos;
        if (mode == "none") {
            turn = w;
            turn.m[3] = {0, 0, 0, 1};
            for (int i = 0; i < 3; ++i) {  // rotation only (the size comes below)
                const float l = std::max(1e-6f, math::length(Vec3{turn.m[i].x, turn.m[i].y, turn.m[i].z}));
                turn.m[i] = {turn.m[i].x / l, turn.m[i].y / l, turn.m[i].z / l, 0.0f};
            }
        } else {
            Vec3 z = mode == "camera" ? to_eye : Vec3{to_eye.x, 0.0f, to_eye.z};
            z = math::length(z) > 1e-5f ? math::normalize(z) : Vec3{0, 0, 1};
            Vec3 x = math::cross(Vec3{0, 1, 0}, z);
            x = math::length(x) > 1e-5f ? math::normalize(x) : Vec3{1, 0, 0};
            const Vec3 y = math::cross(z, x);
            turn.m[0] = {x.x, x.y, x.z, 0};
            turn.m[1] = {y.x, y.y, y.z, 0};
            turn.m[2] = {z.x, z.y, z.z, 0};
        }
        const math::Vec2 size = s.sheet.frame_size_m();
        const bool flip = c.is_object() && c.value("flip_x", false);
        const math::Mat4 place = math::translation(pos) * turn * math::scale({size.x * scale.x, size.y * scale.y, 1.0f}) *
                                 math::translation({-(flip ? 1.0f - s.sheet.pivot.x : s.sheet.pivot.x), -s.sheet.pivot.y, 0.0f});
        render::DrawItem item;
        item.mesh = quad_;
        item.world_from_object = place;
        // 2D part 3 (ADR-040): sorted by layer, order or height ("sort": "y") instead of depth, soft-edged ("blend"),
        // unlit. Any of the first three puts it in the see-through pass, where it's drawn in that order.
        const bool sorted = c.is_object() && (c.contains("layer") || c.contains("order") || c.value("sort", std::string()) == "y");
        const bool soft = c.is_object() && c.value("blend", false);
        const bool unlit = c.is_object() && c.value("unlit", false);
        const int look = (sorted || soft ? 1 : 0) | (unlit ? 2 : 0);
        item.material = material(s, frame, c.is_object() && c.contains("color") ? linear_of(c["color"]) : Vec3{1, 1, 1}, flip, look);
        item.cast_shadow = !c.is_object() || c.value("cast_shadows", true);
        if (c.is_object()) {
            const auto num = [&](const char* k, double fallback) { return c.contains(k) && c[k].is_number() ? c[k].get<double>() : fallback; };
            item.sort_layer = static_cast<std::int32_t>(std::lround(num("layer", 0.0)));
            // "sort": "y": lower on the screen is nearer (top-down, isometric), its pivot's height decides; "order" nudges.
            item.sort_order = c.value("sort", std::string()) == "y" ? -pos.y + static_cast<float>(num("order", 0.0)) * 1e-4f
                                                                     : static_cast<float>(num("order", 0.0));
        }
        out.push_back(item);
    }
}

} // namespace noor::game
