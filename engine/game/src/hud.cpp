#include "noor/game/hud.h"

#include "noor/fonts/hud_font.h"  // k_hud_font: the embedded TTF (generated, see engine/game/CMakeLists.txt)

#include "noor/foundation/log.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace noor::game {
namespace {

// Glyphs are baked at this pixel height (ascent - descent) and scaled for drawing. With a distance
// field that stays sharp both ways; 48 keeps small accents and thin strokes intact.
constexpr float kBakeHeight = 48.0f;
constexpr int kPadding = 6;  // distance field reach around each glyph, in baked pixels
constexpr std::uint32_t kAtlasWidth = 1024;

// Decodes one UTF-8 code point; malformed bytes become U+FFFD.
char32_t next_codepoint(std::string_view s, std::size_t& i) {
    const auto b = static_cast<unsigned char>(s[i++]);
    if (b < 0x80) return b;
    int extra = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : b >= 0xC0 ? 1 : -1;
    if (extra < 0) return 0xFFFD;
    char32_t c = b & (0x3F >> extra);
    for (; extra > 0; --extra) {
        if (i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) return 0xFFFD;
        c = (c << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
    }
    return c;
}

std::vector<char32_t> baked_codepoints() {
    std::vector<char32_t> out;
    for (char32_t c = 32; c < 127; ++c) out.push_back(c);
    for (char32_t c = 160; c < 384; ++c) out.push_back(c);  // Latin-1 Supplement + Latin Extended-A
    for (char32_t c : {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x20AC, 0xFFFD}) out.push_back(c);
    return out;
}

} // namespace

Hud::Hud() {
    stbtt_fontinfo font{};
    if (!stbtt_InitFont(&font, k_hud_font, stbtt_GetFontOffsetForIndex(k_hud_font, 0))) {
        NOOR_LOG_ERROR("Game", "The built-in HUD font couldn't be read: HUD text won't show");
        atlas_ = {0};
        atlas_width_ = atlas_height_ = 1;
        return;
    }
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &gap);
    const float scale = kBakeHeight / static_cast<float>(ascent - descent);
    ascent_ = static_cast<float>(ascent) * scale;
    line_height_ = kBakeHeight;

    // Bake every glyph's distance field and pack them on shelves.
    struct Baked {
        char32_t c;
        unsigned char* bitmap;
        int w, h, xoff, yoff;
    };
    std::vector<Baked> baked;
    for (const char32_t c : baked_codepoints()) {
        const int index = stbtt_FindGlyphIndex(&font, static_cast<int>(c));
        if (index == 0 && c != 0xFFFD) continue;  // not in the font
        int advance = 0, bearing = 0;
        stbtt_GetGlyphHMetrics(&font, index, &advance, &bearing);
        Glyph g;
        g.advance = static_cast<float>(advance) * scale;
        g.index = index;
        int w = 0, h = 0, xoff = 0, yoff = 0;
        unsigned char* bitmap = stbtt_GetGlyphSDF(&font, scale, index, kPadding, 128, 128.0f / kPadding, &w, &h, &xoff, &yoff);
        glyphs_[c] = g;
        if (bitmap) baked.push_back({c, bitmap, w, h, xoff, yoff});
    }
    std::ranges::sort(baked, [](const Baked& a, const Baked& b) { return a.h > b.h; });
    std::uint32_t x = 1, y = 1, shelf = 0;  // 1-texel border: nothing bleeds from the edge
    struct Place {
        std::uint32_t x, y;
    };
    std::vector<Place> places;
    for (const auto& b : baked) {
        if (x + static_cast<std::uint32_t>(b.w) + 1 > kAtlasWidth) {
            x = 1;
            y += shelf + 1;
            shelf = 0;
        }
        places.push_back({x, y});
        x += static_cast<std::uint32_t>(b.w) + 1;
        shelf = std::max(shelf, static_cast<std::uint32_t>(b.h));
    }
    atlas_width_ = kAtlasWidth;
    atlas_height_ = std::bit_ceil(y + shelf + 1);
    atlas_.assign(static_cast<std::size_t>(atlas_width_) * atlas_height_, 0);
    for (std::size_t i = 0; i < baked.size(); ++i) {
        const auto& b = baked[i];
        for (int row = 0; row < b.h; ++row)
            std::memcpy(&atlas_[(places[i].y + static_cast<std::uint32_t>(row)) * atlas_width_ + places[i].x], b.bitmap + row * b.w,
                        static_cast<std::size_t>(b.w));
        Glyph& g = glyphs_[b.c];
        g.x0 = static_cast<float>(b.xoff);
        g.y0 = static_cast<float>(b.yoff);
        g.w = static_cast<float>(b.w);
        g.h = static_cast<float>(b.h);
        g.u0 = static_cast<float>(places[i].x) / static_cast<float>(atlas_width_);
        g.v0 = static_cast<float>(places[i].y) / static_cast<float>(atlas_height_);
        g.u1 = static_cast<float>(places[i].x + static_cast<std::uint32_t>(b.w)) / static_cast<float>(atlas_width_);
        g.v1 = static_cast<float>(places[i].y + static_cast<std::uint32_t>(b.h)) / static_cast<float>(atlas_height_);
        stbtt_FreeSDF(b.bitmap, nullptr);
    }
    // Kerning for the pairs that have it (ASCII letters, digits and punctuation: where it shows).
    for (char32_t a = 32; a < 127; ++a) {
        for (char32_t b = 32; b < 127; ++b) {
            const auto ga = glyphs_.find(a), gb = glyphs_.find(b);
            if (ga == glyphs_.end() || gb == glyphs_.end()) continue;
            const int k = stbtt_GetGlyphKernAdvance(&font, ga->second.index, gb->second.index);
            if (k != 0)
                kerning_[(static_cast<std::uint64_t>(ga->second.index) << 32) | static_cast<std::uint32_t>(gb->second.index)] =
                    static_cast<float>(k) * scale;
        }
    }
}

const Hud::Glyph& Hud::glyph(char32_t c) const {
    if (const auto it = glyphs_.find(c); it != glyphs_.end()) return it->second;
    if (const auto it = glyphs_.find(U'?'); it != glyphs_.end()) return it->second;
    static const Glyph none{};
    return none;
}

float Hud::kern(int a, int b) const {
    const auto it = kerning_.find((static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint32_t>(b));
    return it == kerning_.end() ? 0.0f : it->second;
}

math::Vec2 Hud::measure(std::string_view utf8, float size) const {
    const float k = size / line_height_;
    float widest = 0, line = 0;
    int lines = 1, previous = -1;
    for (std::size_t i = 0; i < utf8.size();) {
        const char32_t c = next_codepoint(utf8, i);
        if (c == U'\n') {
            widest = std::max(widest, line);
            line = 0;
            previous = -1;
            ++lines;
            continue;
        }
        const Glyph& g = glyph(c);
        if (previous >= 0) line += kern(previous, g.index);
        line += g.advance;
        previous = g.index;
    }
    return {std::max(widest, line) * k, static_cast<float>(lines) * size};
}

math::Vec2 Hud::place(runtime::UiAnchor anchor, math::Vec2 position, math::Vec2 extent, math::Vec2 screen) {
    const auto a = static_cast<int>(anchor);
    const float fx = static_cast<float>(a % 3) * 0.5f;  // 0 left, 0.5 centre, 1 right
    const float fy = static_cast<float>(a / 3) * 0.5f;  // 0 top, 0.5 middle, 1 bottom
    return {screen.x * fx + position.x - extent.x * fx, screen.y * fy + position.y - extent.y * fy};
}

void Hud::build(std::span<const runtime::UiCommand> commands, float width, float height, std::vector<render::OverlayVertex>& out) const {
    if (commands.empty() || width <= 0 || height <= 0) return;
    const float unit = height / runtime::kUiHeight;  // pixels per UI unit
    const math::Vec2 screen{width / unit, runtime::kUiHeight};
    auto quad = [&](float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, math::Vec4 color) {
        const render::OverlayVertex a{{x0, y0}, {u0, v0}, color}, b{{x1, y0}, {u1, v0}, color};
        const render::OverlayVertex c{{x1, y1}, {u1, v1}, color}, d{{x0, y1}, {u0, v1}, color};
        out.insert(out.end(), {a, b, c, a, c, d});
    };
    for (const auto& cmd : commands) {
        if (cmd.color.w <= 0) continue;
        if (cmd.kind == runtime::UiCommand::Kind::Rect) {
            const math::Vec2 at = place(cmd.anchor, cmd.position, cmd.size, screen);
            quad(at.x * unit, at.y * unit, (at.x + cmd.size.x) * unit, (at.y + cmd.size.y) * unit, -1, -1, -1, -1, cmd.color);
            continue;
        }
        const math::Vec2 extent = measure(cmd.text, cmd.text_size);
        const math::Vec2 at = place(cmd.anchor, cmd.position, extent, screen);
        const float k = cmd.text_size / line_height_ * unit;  // baked pixels -> framebuffer pixels
        // Lines line up with the block's anchor: left, centred or right-aligned like the anchor.
        const float align = static_cast<float>(static_cast<int>(cmd.anchor) % 3) * 0.5f;
        std::size_t line_start = 0;
        float y = at.y * unit;
        while (line_start <= cmd.text.size()) {
            const std::size_t line_end = std::min(cmd.text.find('\n', line_start), cmd.text.size());
            const std::string_view line(cmd.text.data() + line_start, line_end - line_start);
            const float line_width = measure(line, cmd.text_size).x * unit;
            float pen = at.x * unit + (extent.x * unit - line_width) * align;
            const float baseline = y + ascent_ * k;
            int previous = -1;
            for (std::size_t i = 0; i < line.size();) {
                const Glyph& g = glyph(next_codepoint(line, i));
                if (previous >= 0) pen += kern(previous, g.index) * k;
                if (g.w > 0)
                    quad(pen + g.x0 * k, baseline + g.y0 * k, pen + (g.x0 + g.w) * k, baseline + (g.y0 + g.h) * k, g.u0, g.v0, g.u1, g.v1,
                         cmd.color);
                pen += g.advance * k;
                previous = g.index;
            }
            y += cmd.text_size * unit;
            if (line_end >= cmd.text.size()) break;
            line_start = line_end + 1;
        }
    }
}

} // namespace noor::game
