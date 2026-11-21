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

class RenderInterface final : public Rml::RenderInterface {
public:
    TextureHost host;
    const FileInterface* files = nullptr;
    // Where the context sits on the screen: added to every translation and clip rectangle.
    std::int32_t offset_x = 0, offset_y = 0;

    struct Geometry {
        std::vector<render::UiVertex> vertices;
        std::vector<std::uint32_t> indices;
    };
    std::unordered_map<std::uintptr_t, Geometry> geometry;
    std::uintptr_t next_geometry = 1;
    std::unordered_map<std::uint32_t, std::uint64_t> texture_bytes;  // alive textures: their size

    // This frame's.
    std::vector<render::UiVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<render::UiDraw> draws;
    bool scissor_on = false;
    Rml::Rectanglei scissor;

    void begin_frame() {
        vertices.clear();
        indices.clear();
        draws.clear();
        scissor_on = false;
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
        const auto it = geometry.find(handle);
        if (it == geometry.end() || it->second.indices.empty()) return;
        render::UiDraw d;
        d.first_index = static_cast<std::uint32_t>(indices.size());
        d.index_count = static_cast<std::uint32_t>(it->second.indices.size());
        d.vertex_offset = static_cast<std::int32_t>(vertices.size());
        d.translation = {translation.x + static_cast<float>(offset_x), translation.y + static_cast<float>(offset_y)};
        d.texture = {static_cast<std::uint32_t>(texture)};
        if (scissor_on) {
            d.clip = true;
            d.clip_rect = {scissor.Left() + offset_x, scissor.Top() + offset_y, scissor.Width(), scissor.Height()};
        }
        vertices.insert(vertices.end(), it->second.vertices.begin(), it->second.vertices.end());
        indices.insert(indices.end(), it->second.indices.begin(), it->second.indices.end());
        draws.push_back(d);
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
    impl_->renderer.offset_x = x;
    impl_->renderer.offset_y = y;
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
    return {r.vertices, r.indices, r.draws};
}

UiStats Ui::stats() const {
    const auto& r = impl_->renderer;
    UiStats s;
    s.documents = static_cast<std::uint32_t>(impl_->documents.size());
    s.draws = static_cast<std::uint32_t>(r.draws.size());
    s.vertices = static_cast<std::uint32_t>(r.vertices.size());
    s.textures = static_cast<std::uint32_t>(r.texture_bytes.size());
    for (const auto& [id, bytes] : r.texture_bytes) s.texture_bytes += bytes;
    return s;
}

} // namespace noor::ui
