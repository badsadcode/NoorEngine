// Game UI (ADR-041): RmlUi behind noor::ui. Three interfaces connect it to the engine:
//   render  RmlUi's geometry and textures become a render::UiFrame (indexed triangles, a texture, a translation and a
//           clip rectangle per draw) and textures in the TextureHost (the renderer's bindless table);
//   system  time, and RmlUi's messages into our log;
//   file    reads from inside the project folder only (UTF-8 paths, never outside the root).
#include "noor/ui/ui.h"

#include "noor/fonts/hud_font.h"  // k_hud_font: the built-in font (engine/fonts)
#include "noor/foundation/file_io.h"
#include "noor/foundation/log.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <RmlUi/Core.h>
#include <stb_image.h>  // its implementation is in the engine (asset/texture.cpp)
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <format>
#include <map>
#include <unordered_map>
#include <vector>

namespace noor::ui {

static_assert(sizeof(Rml::Vertex) == sizeof(render::UiVertex), "RmlUi's vertices are copied as they are");
static_assert(offsetof(Rml::Vertex, colour) == offsetof(render::UiVertex, color));
static_assert(offsetof(Rml::Vertex, tex_coord) == offsetof(render::UiVertex, uv));

namespace {

constexpr const char* kBuiltInFamily = "Atkinson Hyperlegible";

// What every document starts from (RmlUi has no built-in style sheet): the usual block elements, the built-in font,
// and a text colour that reads on a dark game. Anything in the document's own styles wins.
constexpr const char* kDefaultStyle = R"(
body, div, p, h1, h2, h3, h4, h5, h6, header, footer, section, nav, ul, ol, li, form, hr, pre {
    display: block;
}
body {
    font-family: "Atkinson Hyperlegible";
    font-size: 24dp;
    color: #EDE8F7;
}
h1 { font-size: 2em; margin: 0.4em 0; }
h2 { font-size: 1.5em; margin: 0.4em 0; }
h3 { font-size: 1.2em; margin: 0.4em 0; }
p { margin: 0.5em 0; }
em { font-style: italic; }
strong { font-weight: bold; }
table { box-sizing: border-box; display: table; }
tr { box-sizing: border-box; display: table-row; }
td { box-sizing: border-box; display: table-cell; }
thead, tbody, tfoot { display: table-row-group; }
col { display: table-column; }
colgroup { display: table-column-group; }

/* The built-in theme (part 6d): what the UI builder's widgets use, so they look right and react with no style sheet.
   Inputs have no look of their own in RmlUi (a slider without styles is invisible); these give them one. */
.noor-panel { display: block; padding: 24dp; background-color: #191623F2; border-radius: 12dp; }
.noor-button { display: block; padding: 10dp 20dp; margin: 8dp 0; text-align: center; background-color: #2A2140;
               border-radius: 8dp; tab-index: auto; focus: auto; nav: auto; }
.noor-button:hover { background-color: #3A2D5C; }
.noor-button:focus { background-color: #4B1AF2; }
.noor-button:active { background-color: #3612C0; }
input { tab-index: auto; nav: auto; }
input.text { display: block; box-sizing: border-box; width: 100%; padding: 6dp 10dp; margin: 4dp 0; background-color: #0E0C16;
             border: 2dp #3A3150; border-radius: 6dp; }
input.text:focus { border-color: #FFC857; }
input.range { display: block; width: 100%; height: 20dp; margin: 8dp 0; }
input.range slidertrack { height: 6dp; margin-top: 7dp; background-color: #3A3150; border-radius: 3dp; }
input.range sliderbar { width: 20dp; height: 20dp; background-color: #EDE8F7; border-radius: 10dp; }
input.range:focus sliderbar { background-color: #FFC857; }
input.range sliderarrowdec, input.range sliderarrowinc { width: 0; height: 0; }
input.checkbox, input.radio { display: inline-block; width: 20dp; height: 20dp; margin-right: 6dp; background-color: #3A3150;
                              border-radius: 4dp; vertical-align: middle; }
input.radio { border-radius: 10dp; }
input.checkbox:checked, input.radio:checked { background-color: #FFC857; }
input.checkbox:focus, input.radio:focus { border: 2dp #EDE8F7; }
)";

// A path RmlUi asks for, as a file inside the root, or nothing when it would leave it.
std::optional<std::filesystem::path> inside(const std::filesystem::path& root, const Rml::String& path) {
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    while (!p.empty() && p.front() == '/') p.erase(p.begin());  // "/UI/x.png": from the root
    if (p.empty()) return std::nullopt;
    const std::filesystem::path rel = std::filesystem::path(std::u8string(p.begin(), p.end())).lexically_normal();
    if (rel.is_absolute() || rel.has_root_name() || rel.empty()) return std::nullopt;
    const auto first = *rel.begin();
    if (first == "..") return std::nullopt;
    return root / rel;
}

struct OpenFile {
    std::string data;
    std::size_t pos = 0;
};

class FileInterface final : public Rml::FileInterface {
public:
    explicit FileInterface(std::filesystem::path root) : root_(std::move(root)) {}

    Rml::FileHandle Open(const Rml::String& path) override {
        const auto file = inside(root_, path);
        if (!file) {
            NOOR_LOG_WARN("UI", "{}: outside the project, not read", path);
            return 0;
        }
        auto text = fs::read_file(*file, 256ull << 20);
        if (!text) return 0;  // RmlUi says what it couldn't load
        return reinterpret_cast<Rml::FileHandle>(new OpenFile{std::move(*text), 0});
    }
    void Close(Rml::FileHandle file) override { delete reinterpret_cast<OpenFile*>(file); }
    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override {
        auto* f = reinterpret_cast<OpenFile*>(file);
        const std::size_t n = std::min(size, f->data.size() - f->pos);
        std::memcpy(buffer, f->data.data() + f->pos, n);
        f->pos += n;
        return n;
    }
    bool Seek(Rml::FileHandle file, long offset, int origin) override {
        auto* f = reinterpret_cast<OpenFile*>(file);
        const long long base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? static_cast<long long>(f->pos) : static_cast<long long>(f->data.size());
        const long long to = base + offset;
        if (to < 0 || to > static_cast<long long>(f->data.size())) return false;
        f->pos = static_cast<std::size_t>(to);
        return true;
    }
    size_t Tell(Rml::FileHandle file) override { return reinterpret_cast<OpenFile*>(file)->pos; }
    size_t Length(Rml::FileHandle file) override { return reinterpret_cast<OpenFile*>(file)->data.size(); }

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

private:
    std::filesystem::path root_;
};

class SystemInterface final : public Rml::SystemInterface {
public:
    std::function<double()> clock;
    bool keyboard = false;  // a text box has focus
    double GetElapsedTime() override { return clock(); }
    std::function<void(const std::string&)> set_clipboard;
    std::function<std::string()> get_clipboard;
    void SetClipboardText(const Rml::String& text) override {
        if (set_clipboard) set_clipboard(text);
        else Rml::SystemInterface::SetClipboardText(text);
    }
    void GetClipboardText(Rml::String& text) override {
        if (get_clipboard) text = get_clipboard();
        else Rml::SystemInterface::GetClipboardText(text);
    }
    void ActivateKeyboard(Rml::Vector2f, float) override { keyboard = true; }
    void DeactivateKeyboard() override { keyboard = false; }
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        switch (type) {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT:
            NOOR_LOG_ERROR("UI", "{}", message);
            break;
        case Rml::Log::LT_WARNING:
            NOOR_LOG_WARN("UI", "{}", message);
            break;
        case Rml::Log::LT_INFO:
            NOOR_LOG_INFO("UI", "{}", message);
            break;
        default:
            NOOR_LOG_DEBUG("UI", "{}", message);
            break;
        }
        return true;
    }
};

// RmlUi's drawing, effects included (6c), recorded as render::UiCommands for the renderer to run in order (vulkan_ui.cpp).
// The meaning of each call follows RmlUi's reference renderer (RmlUi_Renderer_GL3.cpp): the same clip-mask values,
// filter matrices and gradient parameters, so pages look the way RmlUi's documentation shows.
class RenderInterface final : public Rml::RenderInterface {
public:
    TextureHost host;
    const FileInterface* files = nullptr;
    math::Vec2 origin;  // where the context sits on the screen (the frame's origin)

    struct Geometry {
        std::vector<render::UiVertex> vertices;
        std::vector<std::uint32_t> indices;
        std::uint64_t frame = ~0ull;  // the frame it was last copied into (and where)
        std::uint32_t first_index = 0;
        std::int32_t vertex_offset = 0;
    };
    std::unordered_map<std::uintptr_t, Geometry> geometry;
    std::uintptr_t next_geometry = 1;
    std::unordered_map<std::uint32_t, std::uint64_t> texture_bytes;  // alive textures: their size
    std::unordered_map<std::uintptr_t, render::UiFilter> compiled_filters;
    std::unordered_map<std::uintptr_t, render::UiGradient> compiled_shaders;
    std::uintptr_t next_handle = 1;

    // This frame's.
    std::uint64_t frame_number = 0;
    std::vector<render::UiVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<render::UiCommand> commands;
    std::vector<std::array<float, 16>> transforms;
    std::vector<render::UiGradient> gradients;
    std::vector<render::UiFilter> filters;
    bool scissor_on = false;
    Rml::Rectanglei scissor;
    std::int32_t transform = -1;
    bool clip_on = false;
    std::uint8_t clip_ref = 0;
    std::uint32_t layers = 0;  // pushed above the base

    void begin_frame() {
        ++frame_number;
        vertices.clear();
        indices.clear();
        commands.clear();
        transforms.clear();
        gradients.clear();
        filters.clear();
        scissor_on = false;
        transform = -1;
        clip_on = false;
        clip_ref = 0;
        layers = 0;
    }
    [[nodiscard]] render::UiFrame frame() const { return {vertices, indices, commands, transforms, gradients, filters, origin}; }

    // A command carrying the state now: scissor, clip mask, transform.
    render::UiCommand command(render::UiCommand::Kind kind) const {
        render::UiCommand c;
        c.kind = kind;
        c.scissor = scissor_on;
        if (scissor_on) c.rect = {scissor.Left(), scissor.Top(), scissor.Width(), scissor.Height()};
        c.stencil = clip_on;
        c.stencil_ref = clip_ref;
        c.transform = transform;
        return c;
    }
    // The geometry into this frame's buffers (once a frame, however often it's drawn); false if there's none.
    bool place(Rml::CompiledGeometryHandle handle, render::UiCommand& c) {
        const auto it = geometry.find(handle);
        if (it == geometry.end() || it->second.indices.empty()) return false;
        Geometry& g = it->second;
        if (g.frame != frame_number) {
            g.frame = frame_number;
            g.first_index = static_cast<std::uint32_t>(indices.size());
            g.vertex_offset = static_cast<std::int32_t>(vertices.size());
            vertices.insert(vertices.end(), g.vertices.begin(), g.vertices.end());
            indices.insert(indices.end(), g.indices.begin(), g.indices.end());
        }
        c.first_index = g.first_index;
        c.index_count = static_cast<std::uint32_t>(g.indices.size());
        c.vertex_offset = g.vertex_offset;
        return true;
    }

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> v, Rml::Span<const int> i) override {
        Geometry g;
        g.vertices.resize(v.size());
        if (!v.empty()) std::memcpy(g.vertices.data(), v.data(), v.size() * sizeof(Rml::Vertex));
        g.indices.assign(i.begin(), i.end());
        const auto handle = next_geometry++;
        geometry.emplace(handle, std::move(g));
        return handle;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override {
        auto c = command(render::UiCommand::Kind::Draw);
        if (!place(handle, c)) return;
        c.translation = {translation.x, translation.y};
        c.texture = {static_cast<std::uint32_t>(texture)};
        commands.push_back(c);
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override { geometry.erase(handle); }

    Rml::TextureHandle make(std::uint32_t w, std::uint32_t h, std::span<const std::uint8_t> rgba, std::string_view what) {
        if (!host.create || w == 0 || h == 0) return 0;
        auto t = host.create(w, h, rgba);
        if (!t) {
            NOOR_LOG_ERROR("UI", "{}: {}", what, t.error().describe());
            return 0;
        }
        texture_bytes[t->id] = static_cast<std::uint64_t>(w) * h * 4;
        return static_cast<Rml::TextureHandle>(t->id);
    }
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override {
        const auto file = files ? inside(files->root(), source) : std::nullopt;
        if (!file) {
            NOOR_LOG_WARN("UI", "{}: outside the project, not loaded", source);
            return 0;
        }
        auto bytes = fs::read_file(*file, 256ull << 20);
        if (!bytes) {
            NOOR_LOG_WARN("UI", "Image {}: {}", source, bytes.error().describe());
            return 0;
        }
        int w = 0, h = 0, n = 0;
        stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes->data()), static_cast<int>(bytes->size()), &w, &h, &n, 4);
        if (!pixels) {
            NOOR_LOG_WARN("UI", "Image {}: not a picture this can read (PNG, JPEG, TGA, BMP)", source);
            return 0;
        }
        // RmlUi wants colour premultiplied by alpha (sRGB values, as the file has them).
        const std::size_t count = static_cast<std::size_t>(w) * h;
        for (std::size_t i = 0; i < count; ++i) {
            stbi_uc* p = pixels + i * 4;
            const unsigned a = p[3];
            for (int c = 0; c < 3; ++c) p[c] = static_cast<stbi_uc>((p[c] * a + 127) / 255);
        }
        const auto handle = make(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), {pixels, count * 4}, source);
        stbi_image_free(pixels);
        if (handle) dimensions = {w, h};
        return handle;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i size) override {
        return make(static_cast<std::uint32_t>(size.x), static_cast<std::uint32_t>(size.y), {source.data(), source.size()}, "UI texture");
    }
    void ReleaseTexture(Rml::TextureHandle texture) override {
        const auto id = static_cast<std::uint32_t>(texture);
        if (texture_bytes.erase(id) && host.destroy) host.destroy({id});
    }
    void EnableScissorRegion(bool enable) override { scissor_on = enable; }
    void SetScissorRegion(Rml::Rectanglei region) override { scissor = region; }

    // ---- Effects (6c) ----

    void SetTransform(const Rml::Matrix4f* t) override {
        if (!t) {
            transform = -1;
            return;
        }
        std::array<float, 16> m{};
        if constexpr (std::is_same_v<Rml::Matrix4f, Rml::ColumnMajorMatrix4f>) {
            std::memcpy(m.data(), t->data(), sizeof(m));
        } else {
            const Rml::ColumnMajorMatrix4f c = t->Transpose();  // row-major storage: its transpose's data is column-major
            std::memcpy(m.data(), c.data(), sizeof(m));
        }
        transform = static_cast<std::int32_t>(transforms.size());
        transforms.push_back(m);
    }

    void EnableClipMask(bool enable) override { clip_on = enable; }
    void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle handle, Rml::Vector2f translation) override {
        auto c = command(render::UiCommand::Kind::ClipMask);
        if (!place(handle, c)) return;
        c.translation = {translation.x, translation.y};
        switch (operation) {
        case Rml::ClipMaskOperation::Set:
            c.clip_op = render::UiCommand::ClipOp::Set;
            clip_ref = 1;
            break;
        case Rml::ClipMaskOperation::SetInverse:
            c.clip_op = render::UiCommand::ClipOp::SetInverse;
            clip_ref = 1;
            break;
        case Rml::ClipMaskOperation::Intersect:
            c.clip_op = render::UiCommand::ClipOp::Intersect;
            clip_ref = static_cast<std::uint8_t>(std::min(255, clip_ref + 1));
            break;
        }
        commands.push_back(c);
    }

    Rml::LayerHandle PushLayer() override {
        commands.push_back(command(render::UiCommand::Kind::PushLayer));
        return static_cast<Rml::LayerHandle>(++layers);
    }
    void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend,
                         Rml::Span<const Rml::CompiledFilterHandle> filter_handles) override {
        auto c = command(render::UiCommand::Kind::Composite);
        c.index = static_cast<std::uint32_t>(source);
        c.destination = static_cast<std::uint32_t>(destination);
        c.replace = blend == Rml::BlendMode::Replace;
        c.first_filter = static_cast<std::uint32_t>(filters.size());
        for (const auto h : filter_handles)
            if (const auto it = compiled_filters.find(h); it != compiled_filters.end()) filters.push_back(it->second);
        c.filter_count = static_cast<std::uint32_t>(filters.size()) - c.first_filter;
        commands.push_back(c);
    }
    void PopLayer() override {
        commands.push_back(command(render::UiCommand::Kind::PopLayer));
        if (layers > 0) --layers;
    }
    Rml::TextureHandle SaveLayerAsTexture() override {
        if (!scissor_on || scissor.Width() <= 0 || scissor.Height() <= 0) return 0;
        const auto w = static_cast<std::uint32_t>(scissor.Width()), h = static_cast<std::uint32_t>(scissor.Height());
        const std::vector<std::uint8_t> clear(static_cast<std::size_t>(w) * h * 4, 0);
        const auto handle = make(w, h, clear, "UI layer texture");
        if (!handle) return 0;
        auto c = command(render::UiCommand::Kind::SaveTexture);
        c.texture = {static_cast<std::uint32_t>(handle)};
        commands.push_back(c);
        return handle;
    }
    Rml::CompiledFilterHandle SaveLayerAsMaskImage() override {
        commands.push_back(command(render::UiCommand::Kind::SaveMask));
        render::UiFilter f;
        f.kind = render::UiFilter::Kind::MaskImage;
        const auto handle = next_handle++;
        compiled_filters.emplace(handle, f);
        return handle;
    }

    Rml::CompiledFilterHandle CompileFilter(const Rml::String& name, const Rml::Dictionary& p) override {
        render::UiFilter f;
        Rml::Matrix4f m = Rml::Matrix4f::Identity();
        bool matrix = true;
        const float value = Rml::Get(p, "value", 1.0f);
        if (name == "opacity") {
            f.kind = render::UiFilter::Kind::Opacity;
            f.value = value;
            matrix = false;
        } else if (name == "blur") {
            f.kind = render::UiFilter::Kind::Blur;
            f.value = Rml::Get(p, "sigma", 1.0f);
            matrix = false;
        } else if (name == "drop-shadow") {
            f.kind = render::UiFilter::Kind::DropShadow;
            f.value = Rml::Get(p, "sigma", 0.0f);
            const auto c = Rml::Get(p, "color", Rml::Colourb()).ToPremultiplied();
            f.color = {c.red / 255.0f, c.green / 255.0f, c.blue / 255.0f, c.alpha / 255.0f};
            const auto o = Rml::Get(p, "offset", Rml::Vector2f(0.0f));
            f.offset = {o.x, o.y};
            matrix = false;
        } else if (name == "brightness") {
            m = Rml::Matrix4f::Diag(value, value, value, 1.0f);
        } else if (name == "contrast") {
            const float grey = 0.5f - 0.5f * value;
            m = Rml::Matrix4f::Diag(value, value, value, 1.0f);
            m.SetColumn(3, Rml::Vector4f(grey, grey, grey, 1.0f));
        } else if (name == "invert") {
            const float v = std::clamp(value, 0.0f, 1.0f), inverted = 1.0f - 2.0f * v;
            m = Rml::Matrix4f::Diag(inverted, inverted, inverted, 1.0f);
            m.SetColumn(3, Rml::Vector4f(v, v, v, 1.0f));
        } else if (name == "grayscale") {
            const float rev = 1.0f - value;
            const Rml::Vector3f g = value * Rml::Vector3f(0.2126f, 0.7152f, 0.0722f);
            m = Rml::Matrix4f::FromRows({g.x + rev, g.y, g.z, 0}, {g.x, g.y + rev, g.z, 0}, {g.x, g.y, g.z + rev, 0}, {0, 0, 0, 1});
        } else if (name == "sepia") {
            const float rev = 1.0f - value;
            const Rml::Vector3f r = value * Rml::Vector3f(0.393f, 0.769f, 0.189f);
            const Rml::Vector3f g = value * Rml::Vector3f(0.349f, 0.686f, 0.168f);
            const Rml::Vector3f b = value * Rml::Vector3f(0.272f, 0.534f, 0.131f);
            m = Rml::Matrix4f::FromRows({r.x + rev, r.y, r.z, 0}, {g.x, g.y + rev, g.z, 0}, {b.x, b.y, b.z + rev, 0}, {0, 0, 0, 1});
        } else if (name == "hue-rotate") {  // https://www.w3.org/TR/filter-effects-1/#attr-valuedef-type-huerotate
            const float s = std::sin(value), c = std::cos(value);
            m = Rml::Matrix4f::FromRows({0.213f + 0.787f * c - 0.213f * s, 0.715f - 0.715f * c - 0.715f * s, 0.072f - 0.072f * c + 0.928f * s, 0},
                                        {0.213f - 0.213f * c + 0.143f * s, 0.715f + 0.285f * c + 0.140f * s, 0.072f - 0.072f * c - 0.283f * s, 0},
                                        {0.213f - 0.213f * c - 0.787f * s, 0.715f - 0.715f * c + 0.715f * s, 0.072f + 0.928f * c + 0.072f * s, 0},
                                        {0, 0, 0, 1});
        } else if (name == "saturate") {
            m = Rml::Matrix4f::FromRows({0.213f + 0.787f * value, 0.715f - 0.715f * value, 0.072f - 0.072f * value, 0},
                                        {0.213f - 0.213f * value, 0.715f + 0.285f * value, 0.072f - 0.072f * value, 0},
                                        {0.213f - 0.213f * value, 0.715f - 0.715f * value, 0.072f + 0.928f * value, 0}, {0, 0, 0, 1});
        } else {
            NOOR_LOG_WARN("UI", "The filter '{}' isn't supported", name);
            return 0;
        }
        if (matrix) {
            f.kind = render::UiFilter::Kind::ColorMatrix;
            const Rml::ColumnMajorMatrix4f cm = m;
            std::memcpy(f.matrix.data(), cm.data(), sizeof(f.matrix));
        }
        const auto handle = next_handle++;
        compiled_filters.emplace(handle, f);
        return handle;
    }
    void ReleaseFilter(Rml::CompiledFilterHandle filter) override { compiled_filters.erase(filter); }

    Rml::CompiledShaderHandle CompileShader(const Rml::String& name, const Rml::Dictionary& p) override {
        using K = render::UiGradient::Kind;
        render::UiGradient g;
        const bool repeating = Rml::Get(p, "repeating", false);
        if (name == "linear-gradient") {
            g.kind = repeating ? K::RepeatingLinear : K::Linear;
            const auto p0 = Rml::Get(p, "p0", Rml::Vector2f(0.0f)), p1 = Rml::Get(p, "p1", Rml::Vector2f(0.0f));
            g.p = {p0.x, p0.y};
            g.v = {p1.x - p0.x, p1.y - p0.y};
        } else if (name == "radial-gradient") {
            g.kind = repeating ? K::RepeatingRadial : K::Radial;
            const auto c = Rml::Get(p, "center", Rml::Vector2f(0.0f)), r = Rml::Get(p, "radius", Rml::Vector2f(1.0f));
            g.p = {c.x, c.y};
            g.v = {1.0f / std::max(r.x, 1e-6f), 1.0f / std::max(r.y, 1e-6f)};
        } else if (name == "conic-gradient") {
            g.kind = repeating ? K::RepeatingConic : K::Conic;
            const auto c = Rml::Get(p, "center", Rml::Vector2f(0.0f));
            const float angle = Rml::Get(p, "angle", 0.0f);
            g.p = {c.x, c.y};
            g.v = {std::cos(angle), std::sin(angle)};
        } else {
            NOOR_LOG_WARN("UI", "The shader '{}' isn't supported (gradients are)", name);
            return 0;
        }
        const auto it = p.find("color_stop_list");
        if (it == p.end() || it->second.GetType() != Rml::Variant::COLORSTOPLIST) return 0;
        const auto& stops = it->second.GetReference<Rml::ColorStopList>();
        g.stops = static_cast<std::uint32_t>(std::min<std::size_t>(stops.size(), render::UiGradient::kMaxStops));
        for (std::uint32_t i = 0; i < g.stops; ++i) {
            const auto& s = stops[i];
            g.positions[i] = s.position.number;
            g.colors[i] = {s.color.red / 255.0f, s.color.green / 255.0f, s.color.blue / 255.0f, s.color.alpha / 255.0f};
        }
        const auto handle = next_handle++;
        compiled_shaders.emplace(handle, g);
        return handle;
    }
    void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle) override {
        const auto it = compiled_shaders.find(shader);
        if (it == compiled_shaders.end()) return;
        auto c = command(render::UiCommand::Kind::Gradient);
        if (!place(handle, c)) return;
        c.translation = {translation.x, translation.y};
        c.index = static_cast<std::uint32_t>(gradients.size());
        gradients.push_back(it->second);
        commands.push_back(c);
    }
    void ReleaseShader(Rml::CompiledShaderHandle shader) override { compiled_shaders.erase(shader); }
};

// Gives every document the default style sheet, first (so the document's own win), as it loads: RmlUi tells plugins
// about a loaded document before it styles and lays it out the first time, so no element is ever without a font.
class DefaultStyle final : public Rml::Plugin {
public:
    Rml::SharedPtr<Rml::StyleSheetContainer> sheet;
    int GetEventClasses() override { return EVT_DOCUMENT; }
    void OnDocumentLoad(Rml::ElementDocument* doc) override {
        if (!sheet) return;
        if (const auto* own = doc->GetStyleSheetContainer())
            doc->SetStyleSheetContainer(sheet->CombineStyleSheetContainer(*own));
        else
            doc->SetStyleSheetContainer(sheet->CombineStyleSheetContainer(Rml::StyleSheetContainer()));
    }
};

Ui* g_live = nullptr;  // RmlUi is one per process

// The events scripts can listen to (part 6b), caught on each document in the capture phase so non-bubbling ones (focus,
// blur) are seen too.
constexpr Rml::EventId kEvents[] = {Rml::EventId::Click, Rml::EventId::Change, Rml::EventId::Submit, Rml::EventId::Focus,
                                    Rml::EventId::Blur, Rml::EventId::Mouseover, Rml::EventId::Mouseout};

// A form control's value as JSON (Ui::value).
asset::Json value_of(Rml::Element* e) {
    if (auto* input = dynamic_cast<Rml::ElementFormControlInput*>(e)) {
        const auto type = input->GetAttribute<Rml::String>("type", "text");
        if (type == "checkbox" || type == "radio") return input->HasAttribute("checked");
        if (type == "range") {
            const auto v = input->GetValue();
            char* end = nullptr;
            const double d = std::strtod(v.c_str(), &end);
            return end != v.c_str() ? asset::Json(d) : asset::Json(0.0);
        }
        return input->GetValue();
    }
    if (auto* control = dynamic_cast<Rml::ElementFormControl*>(e)) return control->GetValue();
    return e->GetInnerRML();
}

// The id of the element or of its nearest ancestor with one.
std::string id_of(Rml::Element* e) {
    for (; e; e = e->GetParentNode())
        if (!e->GetId().empty()) return e->GetId();
    return {};
}

std::string escaped(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

class EventCatcher final : public Rml::EventListener {
public:
    std::vector<UiEvent>* events = nullptr;
    std::uint32_t document = 0;
    void ProcessEvent(Rml::Event& event) override {
        if (!events || events->size() >= 4096) return;
        UiEvent e;
        e.document = {document};
        e.type = event.GetType();
        Rml::Element* target = event.GetTargetElement();
        e.element = id_of(target);
        if (event.GetId() == Rml::EventId::Change && target) {
            e.value = value_of(target);
        } else if (event.GetId() == Rml::EventId::Submit) {
            asset::Json fields = asset::Json::object();
            for (const auto& [name, v] : event.GetParameters())
                if (name != "submit") fields[name] = v.Get<Rml::String>();
            e.value = std::move(fields);
        }
        events->push_back(std::move(e));
    }
};

} // namespace

TextureHost renderer_textures(render::Renderer& renderer) {
    TextureHost host;
    host.create = [&renderer](std::uint32_t w, std::uint32_t h, std::span<const std::uint8_t> rgba) {
        const render::TextureMipData mip{w, h, rgba};
        return renderer.create_texture({render::TextureFormat::Rgba8Unorm, {&mip, 1}, "UI texture"});
    };
    host.destroy = [&renderer](render::TextureHandle t) { renderer.destroy_texture(t); };
    return host;
}

struct Ui::Impl {
    FileInterface files;
    SystemInterface system;
    RenderInterface renderer;
    Rml::Context* context = nullptr;
    DefaultStyle default_style;
    struct Document {
        Rml::ElementDocument* doc = nullptr;
        std::string path;
        std::unique_ptr<EventCatcher> catcher;
    };
    std::map<std::uint32_t, Document> documents;  // ordered: find() gives the first opened
    std::uint32_t next_document = 1;
    std::int32_t x = 0, y = 0, width = 0, height = 0;
    std::vector<UiEvent> events;

    explicit Impl(std::filesystem::path root) : files(std::move(root)) {}

    Rml::ElementDocument* find(DocumentId d) const {
        const auto it = documents.find(d.id);
        return it == documents.end() ? nullptr : it->second.doc;
    }
    Rml::Element* element(DocumentId d, std::string_view id) const {
        auto* doc = find(d);
        if (!doc || id.empty()) return nullptr;
        if (id.front() == '#') id.remove_prefix(1);
        if (id.empty()) return nullptr;
        return doc->GetElementById(Rml::String(id));
    }
};

Result<std::unique_ptr<Ui>> Ui::create(UiDesc desc) {
    if (g_live) return fail(ErrorCode::AlreadyExists, "Only one game UI can exist at a time (RmlUi keeps global state)");
    auto impl = std::make_unique<Impl>(desc.root);
    if (desc.clock) {
        impl->system.clock = std::move(desc.clock);
    } else {
        const auto start = std::chrono::steady_clock::now();
        impl->system.clock = [start] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); };
    }
    impl->system.set_clipboard = std::move(desc.set_clipboard);
    impl->system.get_clipboard = std::move(desc.get_clipboard);
    impl->renderer.host = std::move(desc.textures);
    impl->renderer.files = &impl->files;
    Rml::SetFileInterface(&impl->files);
    Rml::SetSystemInterface(&impl->system);
    Rml::SetRenderInterface(&impl->renderer);
    if (!Rml::Initialise()) {
        Rml::SetFileInterface(nullptr);
        Rml::SetSystemInterface(nullptr);
        Rml::SetRenderInterface(nullptr);
        return fail(ErrorCode::Unknown, "RmlUi couldn't start (see the log)");
    }
    // The built-in font, so every page has text even before it loads a font of its own.
    if (!Rml::LoadFontFace({k_hud_font, sizeof(k_hud_font)}, kBuiltInFamily, Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, true))
        NOOR_LOG_ERROR("UI", "The built-in font couldn't be loaded: UI text won't show");
    impl->default_style.sheet = Rml::Factory::InstanceStyleSheetString(kDefaultStyle);
    Rml::RegisterPlugin(&impl->default_style);
    impl->context = Rml::CreateContext("noor", {1280, 720});
    if (!impl->context) {
        Rml::Shutdown();
        return fail(ErrorCode::Unknown, "RmlUi couldn't make its context");
    }
    auto ui = std::unique_ptr<Ui>(new Ui(std::move(impl)));
    ui->set_viewport(0, 0, 1280, 720);
    g_live = ui.get();
    return ui;
}

Ui::Ui(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Ui::~Ui() {
    if (!impl_) return;
    for (auto& [id, d] : impl_->documents)  // our listeners go before the documents do
        for (const auto event : kEvents) d.doc->RemoveEventListener(event, d.catcher.get(), true);
    impl_->documents.clear();
    if (impl_->context) Rml::RemoveContext(impl_->context->GetName());
    Rml::UnregisterPlugin(&impl_->default_style);
    impl_->default_style.sheet.reset();
    Rml::Shutdown();  // releases its textures and geometry through our interfaces, which live until here
    Rml::SetFileInterface(nullptr);
    Rml::SetSystemInterface(nullptr);
    Rml::SetRenderInterface(nullptr);
    if (g_live == this) g_live = nullptr;
}

Status Ui::load_font(std::string_view path, bool fallback) {
    const auto file = inside(impl_->files.root(), Rml::String(path));
    if (!file) return fail(ErrorCode::InvalidArgument, std::format("Font {}: not a file inside the project", path));
    if (!std::filesystem::exists(*file)) return fail(ErrorCode::NotFound, std::format("Font {}: no such file", path));
    if (!Rml::LoadFontFace(Rml::String(path), fallback))
        return fail(ErrorCode::ParseError, std::format("Font {}: not a font RmlUi can read (TrueType or OpenType)", path));
    return {};
}

Result<DocumentId> Ui::open(std::string_view path) {
    const auto file = inside(impl_->files.root(), Rml::String(path));
    if (!file) return fail(ErrorCode::InvalidArgument, std::format("UI document {}: not a file inside the project", path));
    if (!std::filesystem::exists(*file)) return fail(ErrorCode::NotFound, std::format("UI document {}: no such file", path));
    Rml::ElementDocument* doc = impl_->context->LoadDocument(Rml::String(path));
    if (!doc) return fail(ErrorCode::ParseError, std::format("UI document {}: couldn't be read (see the log)", path));
    doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::Auto);
    const DocumentId id{impl_->next_document++};
    auto catcher = std::make_unique<EventCatcher>();
    catcher->events = &impl_->events;
    catcher->document = id.id;
    for (const auto event : kEvents) doc->AddEventListener(event, catcher.get(), true);
    impl_->documents.emplace(id.id, Impl::Document{doc, std::string(path), std::move(catcher)});
    return id;
}

void Ui::close(DocumentId document) {
    const auto it = impl_->documents.find(document.id);
    if (it == impl_->documents.end()) return;
    for (const auto event : kEvents) it->second.doc->RemoveEventListener(event, it->second.catcher.get(), true);
    it->second.doc->Close();  // RmlUi removes it at its next update
    impl_->documents.erase(it);
}

bool Ui::is_open(DocumentId document) const { return impl_->find(document) != nullptr; }

std::optional<DocumentId> Ui::find(std::string_view path) const {
    std::string_view p = path;
    while (!p.empty() && p.front() == '/') p.remove_prefix(1);
    for (const auto& [id, d] : impl_->documents) {
        std::string_view q = d.path;
        while (!q.empty() && q.front() == '/') q.remove_prefix(1);
        if (q == p) return DocumentId{id};
    }
    return std::nullopt;
}

bool Ui::set_text(DocumentId document, std::string_view element, std::string_view text) {
    return set_rml(document, element, escaped(text));
}
bool Ui::set_rml(DocumentId document, std::string_view element, std::string_view rml) {
    auto* e = impl_->element(document, element);
    if (e) e->SetInnerRML(Rml::String(rml));
    return e != nullptr;
}
bool Ui::set_class(DocumentId document, std::string_view element, std::string_view name, bool on) {
    auto* e = impl_->element(document, element);
    if (e) e->SetClass(Rml::String(name), on);
    return e != nullptr;
}
bool Ui::set_attribute(DocumentId document, std::string_view element, std::string_view name, const asset::Json& value) {
    auto* e = impl_->element(document, element);
    if (!e) return false;
    if (value.is_null() || (value.is_boolean() && !value.get<bool>())) e->RemoveAttribute(Rml::String(name));
    else if (value.is_string()) e->SetAttribute(Rml::String(name), value.get<std::string>());
    else if (value.is_boolean()) e->SetAttribute(Rml::String(name), Rml::String());  // present: "disabled", "checked"
    else e->SetAttribute(Rml::String(name), value.dump());
    return true;
}
bool Ui::set_style(DocumentId document, std::string_view element, std::string_view property, std::string_view value) {
    auto* e = impl_->element(document, element);
    if (!e) return false;
    if (value.empty()) e->RemoveProperty(Rml::String(property));
    else if (!e->SetProperty(Rml::String(property), Rml::String(value)))
        NOOR_LOG_WARN("UI", "#{}: '{}: {}' isn't a style property RmlUi understands", element, property, value);
    return true;
}
std::optional<asset::Json> Ui::value(DocumentId document, std::string_view element) const {
    auto* e = impl_->element(document, element);
    if (!e) return std::nullopt;
    return value_of(e);
}
bool Ui::set_value(DocumentId document, std::string_view element, const asset::Json& value) {
    auto* e = impl_->element(document, element);
    if (!e) return false;
    if (auto* input = dynamic_cast<Rml::ElementFormControlInput*>(e)) {
        const auto type = input->GetAttribute<Rml::String>("type", "text");
        if (type == "checkbox" || type == "radio") {
            const bool on = value.is_boolean() ? value.get<bool>() : !value.is_null();
            if (on) input->SetAttribute("checked", Rml::String());
            else input->RemoveAttribute("checked");
            return true;
        }
    }
    const std::string text = value.is_string() ? value.get<std::string>() : value.is_null() ? std::string() : value.dump();
    if (auto* control = dynamic_cast<Rml::ElementFormControl*>(e)) control->SetValue(text);
    else e->SetInnerRML(escaped(text));
    return true;
}
bool Ui::focus(DocumentId document, std::string_view element) {
    auto* e = impl_->element(document, element);
    return e && e->Focus(true);
}
bool Ui::click(DocumentId document, std::string_view element) {
    auto* e = impl_->element(document, element);
    if (e) e->Click();
    return e != nullptr;
}

std::vector<UiEvent> Ui::take_events() { return std::exchange(impl_->events, {}); }

namespace {
// The elements among a parent's children, as the page's markup has them (no text, no inputs' insides).
std::vector<Rml::Element*> markup_children(Rml::Element* parent) {
    std::vector<Rml::Element*> out;
    for (int i = 0; i < parent->GetNumChildren(false); ++i) {
        Rml::Element* c = parent->GetChild(i);
        if (c && c->GetTagName() != "#text") out.push_back(c);
    }
    return out;
}
} // namespace

std::optional<Ui::ElementAt> Ui::element_at(float x, float y) const {
    Rml::Element* e = impl_->context->GetElementAtPoint({x - static_cast<float>(impl_->x), y - static_cast<float>(impl_->y)});
    if (!e) return std::nullopt;
    Rml::ElementDocument* doc = e->GetOwnerDocument();
    std::optional<DocumentId> id;
    for (const auto& [n, d] : impl_->documents)
        if (d.doc == doc) id = DocumentId{n};
    if (!id) return std::nullopt;
    // Up to the document, past what the markup doesn't have (text, an input's slider parts).
    std::vector<int> path;
    for (Rml::Element* at = e; at && at != doc;) {
        Rml::Element* parent = at->GetParentNode();
        if (!parent) return std::nullopt;
        const auto siblings = markup_children(parent);
        const auto it = std::find(siblings.begin(), siblings.end(), at);
        if (it == siblings.end()) {
            path.clear();  // inside something the markup doesn't show: that something is what was hit
        } else {
            path.insert(path.begin(), static_cast<int>(it - siblings.begin()));
        }
        at = parent;
    }
    return ElementAt{*id, path};
}

std::optional<std::array<float, 4>> Ui::element_box(DocumentId document, const std::vector<int>& path) const {
    Rml::Element* e = impl_->find(document);
    if (!e) return std::nullopt;
    for (const int i : path) {
        const auto children = markup_children(e);
        if (i < 0 || static_cast<std::size_t>(i) >= children.size()) return std::nullopt;
        e = children[static_cast<std::size_t>(i)];
    }
    const auto at = e->GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto size = e->GetBox().GetSize(Rml::BoxArea::Border);
    return std::array<float, 4>{at.x + static_cast<float>(impl_->x), at.y + static_cast<float>(impl_->y), size.x, size.y};
}

void Ui::key(Key key, bool down, Modifiers modifiers) {
    static constexpr Rml::Input::KeyIdentifier kKeys[] = {
        Rml::Input::KI_TAB,   Rml::Input::KI_RETURN, Rml::Input::KI_SPACE, Rml::Input::KI_ESCAPE, Rml::Input::KI_BACK,
        Rml::Input::KI_DELETE, Rml::Input::KI_LEFT,  Rml::Input::KI_RIGHT, Rml::Input::KI_UP,     Rml::Input::KI_DOWN,
        Rml::Input::KI_HOME,  Rml::Input::KI_END,    Rml::Input::KI_PRIOR, Rml::Input::KI_NEXT,   Rml::Input::KI_A,
        Rml::Input::KI_C,     Rml::Input::KI_V,      Rml::Input::KI_X};
    const auto k = kKeys[static_cast<std::size_t>(key)];
    int mods = 0;
    if (modifiers.ctrl) mods |= Rml::Input::KM_CTRL;
    if (modifiers.shift) mods |= Rml::Input::KM_SHIFT;
    if (modifiers.alt) mods |= Rml::Input::KM_ALT;
    if (down) impl_->context->ProcessKeyDown(k, mods);
    else impl_->context->ProcessKeyUp(k, mods);
}

void Ui::text(std::string_view utf8) {
    if (!utf8.empty()) impl_->context->ProcessTextInput(Rml::String(utf8));
}

bool Ui::wants_text() const { return impl_->system.keyboard; }

void Ui::set_visible(DocumentId document, bool visible) {
    if (auto* doc = impl_->find(document)) {
        if (visible) doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::Auto);
        else doc->Hide();
    }
}

bool Ui::visible(DocumentId document) const {
    const auto* doc = impl_->find(document);
    return doc && doc->IsVisible();
}

void Ui::set_viewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (x == impl_->x && y == impl_->y && width == impl_->width && height == impl_->height) return;
    impl_->x = x, impl_->y = y, impl_->width = width, impl_->height = height;
    impl_->renderer.origin = {static_cast<float>(x), static_cast<float>(y)};
    impl_->context->SetDimensions({width, height});
    impl_->context->SetDensityIndependentPixelRatio(static_cast<float>(height) / 720.0f);  // dp: the HUD's unit
}

void Ui::mouse_move(float x, float y) {
    impl_->context->ProcessMouseMove(static_cast<int>(x) - impl_->x, static_cast<int>(y) - impl_->y, 0);
}
void Ui::mouse_button(int button, bool down) {
    if (down) impl_->context->ProcessMouseButtonDown(button, 0);
    else impl_->context->ProcessMouseButtonUp(button, 0);
}
void Ui::mouse_wheel(float lines) { impl_->context->ProcessMouseWheel(lines, 0); }

bool Ui::mouse_over_ui() const { return impl_->context->IsMouseInteracting(); }

void Ui::update() { impl_->context->Update(); }

render::UiFrame Ui::draw() {
    auto& r = impl_->renderer;
    r.begin_frame();
    impl_->context->Render();
    return r.frame();
}

UiStats Ui::stats() const {
    const auto& r = impl_->renderer;
    UiStats s;
    s.documents = static_cast<std::uint32_t>(impl_->documents.size());
    for (const auto& c : r.commands)
        if (c.kind == render::UiCommand::Kind::Draw || c.kind == render::UiCommand::Kind::Gradient) ++s.draws;
    s.vertices = static_cast<std::uint32_t>(r.vertices.size());
    s.textures = static_cast<std::uint32_t>(r.texture_bytes.size());
    for (const auto& [id, bytes] : r.texture_bytes) s.texture_bytes += bytes;
    return s;
}

} // namespace noor::ui
