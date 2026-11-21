// Spike B (ADR-041): the same pause menu with RmlUi (MIT; HTML/CSS-like documents), its fonts from FreeType. RmlUi's
// render interface hands us triangles and textures; here they go to the spike's CPU rasterizer. A real integration
// would hand them to the Vulkan renderer instead: textured, scissored overlay geometry, which the HUD pass doesn't do
// today (it has one font atlas and no scissor).
#include "spike.h"

#include <RmlUi/Core.h>

#include <chrono>
#include <map>

namespace ui_spike {

namespace {

struct Geometry {
    std::vector<Rml::Vertex> vertices;
    std::vector<int> indices;
};

class Renderer final : public Rml::RenderInterface {
public:
    std::map<std::uintptr_t, Geometry> geometry;
    std::map<std::uintptr_t, Texture> textures;
    struct Call {
        std::uintptr_t geometry, texture;
        Rml::Vector2f translation;
        std::array<int, 4> scissor;
    };
    std::vector<Call> calls;  // this frame's, in order
    std::uintptr_t next = 1;
    bool scissor_on = false;
    std::array<int, 4> scissor{0, 0, 1 << 20, 1 << 20};

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override {
        const auto h = next++;
        geometry[h] = {std::vector<Rml::Vertex>(vertices.begin(), vertices.end()), std::vector<int>(indices.begin(), indices.end())};
        return h;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle g, Rml::Vector2f translation, Rml::TextureHandle texture) override {
        calls.push_back({g, texture, translation, scissor_on ? scissor : std::array<int, 4>{0, 0, 1 << 20, 1 << 20}});
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle g) override { geometry.erase(g); }
    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }  // no images in this menu
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i size) override {
        const auto h = next++;
        Texture t;
        t.width = size.x;
        t.height = size.y;
        t.channels = 4;
        t.pixels.assign(source.begin(), source.end());
        textures[h] = std::move(t);
        return h;
    }
    void ReleaseTexture(Rml::TextureHandle t) override { textures.erase(t); }
    void EnableScissorRegion(bool on) override { scissor_on = on; }
    void SetScissorRegion(Rml::Rectanglei r) override { scissor = {r.Left(), r.Top(), r.Right(), r.Bottom()}; }
};

class System final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override { return 0.0; }  // a still picture: no animations running
};

} // namespace

SpikeResult rml_pause_menu(const std::string& rml_path, const std::string& font_path, int focus, int width, int height) {
    static Renderer renderer;
    static System system;
    static bool ready = false;
    if (!ready) {
        Rml::SetRenderInterface(&renderer);
        Rml::SetSystemInterface(&system);
        Rml::Initialise();
        Rml::LoadFontFace(font_path);
        ready = true;
    }
    SpikeResult r;
    Rml::Context* context = Rml::CreateContext("spike", Rml::Vector2i(width, height));
    // The HUD's units are 720 to the screen's height; RmlUi's dp follow the density ratio: the same sizes at any height.
    context->SetDensityIndependentPixelRatio(static_cast<float>(height) / 720.0f);
    Rml::ElementDocument* doc = context->LoadDocument(rml_path);
    if (!doc) return r;
    doc->Show();
    const char* ids[] = {"resume", "settings", "quit"};
    if (auto* b = doc->GetElementById(ids[std::clamp(focus, 0, 2)])) b->Focus();
    const auto t0 = std::chrono::steady_clock::now();
    constexpr int kRuns = 1000;
    for (int run = 0; run < kRuns; ++run) {
        renderer.calls.clear();
        context->Update();
        context->Render();
    }
    r.ms_per_frame = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / kRuns;
    for (const auto& c : renderer.calls) r.triangles += renderer.geometry[c.geometry].indices.size() / 3;
    const auto calls = renderer.calls;
    r.draw = [calls](Image& img) {
        for (const auto& c : calls) {
            const auto g = renderer.geometry.find(c.geometry);
            if (g == renderer.geometry.end()) continue;
            const Texture* tex = nullptr;
            if (const auto t = renderer.textures.find(c.texture); c.texture && t != renderer.textures.end()) tex = &t->second;
            std::array<int, 4> s = c.scissor;
            s[2] = std::min(s[2], img.width), s[3] = std::min(s[3], img.height);
            for (std::size_t i = 0; i + 2 < g->second.indices.size(); i += 3) {
                Vert v[3];
                for (int k = 0; k < 3; ++k) {
                    const auto& o = g->second.vertices[static_cast<std::size_t>(g->second.indices[i + k])];
                    v[k] = {o.position.x + c.translation.x, o.position.y + c.translation.y, o.tex_coord.x, o.tex_coord.y,
                            o.colour.red / 255.0f, o.colour.green / 255.0f, o.colour.blue / 255.0f, o.colour.alpha / 255.0f};
                }
                img.triangle(v[0], v[1], v[2], tex, false, true, s);  // RmlUi: sRGB colours, premultiplied alpha
            }
        }
    };
    return r;
}

} // namespace ui_spike
