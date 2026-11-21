#pragma once
// Game UI (gameplay foundations part 6, ADR-041): `.noorui` documents (RML: HTML-like markup, styled with RCSS: CSS-like)
// laid out, styled and drawn by RmlUi behind this API, so RmlUi stays an implementation detail. The HUD (noor.ui.text,
// noor.ui.rect) is separate and stays.
//
// What a document can reach: files inside the project (`root`): its style sheets, fonts (@font-face) and images, by
// paths relative to the document or to the root ("/UI/x.png"); nothing outside it. Every document starts from a small
// default style sheet (blocks for body, div, p, h1..h6, the built-in font, a light text colour), which its own styles
// override.
//
// Units: `dp` is the HUD's unit, 1/720 of the picture's height, so a page looks the same at any resolution; `px` are
// real pixels. Positions given to the input calls are screen pixels (the viewport's offset is taken off here).
//
// One Ui at a time: RmlUi keeps global state (create fails while another exists).

#include "noor/asset/json.h"
#include "noor/foundation/error.h"
#include "noor/render/renderer.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <span>
#include <string_view>
#include <vector>

namespace noor::ui {

// Where the UI's textures go (font atlases, images): the renderer's table, or a test's stand-in. `rgba` is width x
// height x 4 bytes, sRGB values premultiplied by alpha (render::UiCommand::texture).
struct TextureHost {
    std::function<Result<render::TextureHandle>(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba)> create;
    std::function<void(render::TextureHandle)> destroy;
};
// The renderer's texture table (Rgba8Unorm, one mip). The renderer must outlive the Ui.
[[nodiscard]] TextureHost renderer_textures(render::Renderer& renderer);

struct UiDesc {
    std::filesystem::path root;  // the project folder: documents and what they load come from inside it
    TextureHost textures;
    std::function<double()> clock;  // seconds, for transitions and animations; empty: real time since create
    // The system clipboard (text boxes' Ctrl+C / V / X); empty: a clipboard of the UI's own.
    std::function<void(const std::string&)> set_clipboard;
    std::function<std::string()> get_clipboard;
};

struct DocumentId {
    std::uint32_t id = 0;
    [[nodiscard]] bool valid() const noexcept { return id != 0; }
    friend bool operator==(DocumentId, DocumentId) = default;
};

// Something that happened on an element (part 6b): a click, a changed value, a submitted form, focus coming or going,
// the mouse over or out. `element` is the id of the element it happened on, or of its nearest ancestor with one.
struct UiEvent {
    DocumentId document;
    std::string type;     // click, change, submit, focus, blur, mouseover, mouseout
    std::string element;  // empty: no element with an id there
    asset::Json value;    // change: the control's value (as value() gives it); submit: the form's fields; else null
};

// Keys the UI listens to: focus moves (Tab, arrows with `nav` properties), Enter and Space click, text editing.
enum class Key : std::uint8_t {
    Tab, Enter, Space, Escape, Backspace, Delete, Left, Right, Up, Down, Home, End, PageUp, PageDown,
    A, C, V, X,  // with Ctrl: select all, copy, paste, cut
};
struct Modifiers {
    bool ctrl = false, shift = false, alt = false;
};

struct UiStats {
    std::uint32_t documents = 0;  // open (shown or hidden)
    std::uint32_t draws = 0;      // in the last draw()
    std::uint32_t vertices = 0;
    std::uint32_t textures = 0;   // alive now
    std::uint64_t texture_bytes = 0;
};

class Ui {
public:
    [[nodiscard]] static Result<std::unique_ptr<Ui>> create(UiDesc desc);
    ~Ui();
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;

    // A font file (.ttf, .otf) inside the root; its family name comes from the file. `fallback`: used for characters
    // the page's fonts don't have. Documents can load their own with @font-face too.
    [[nodiscard]] Status load_font(std::string_view path, bool fallback = false);

    // Opens a document (a path inside the root, e.g. "UI/pause.noorui") and shows it. Fails with the reason when the
    // file is missing or isn't a document; problems inside it (a bad property) are logged as warnings.
    [[nodiscard]] Result<DocumentId> open(std::string_view path);
    void close(DocumentId document);
    [[nodiscard]] bool is_open(DocumentId document) const;
    void set_visible(DocumentId document, bool visible);
    [[nodiscard]] bool visible(DocumentId document) const;
    // An open document by the path it was opened with.
    [[nodiscard]] std::optional<DocumentId> find(std::string_view path) const;

    // Elements by id (part 6b). False, changing nothing, when the document or element isn't there.
    bool set_text(DocumentId document, std::string_view element, std::string_view text);  // shown as it is (no markup)
    bool set_rml(DocumentId document, std::string_view element, std::string_view rml);
    bool set_class(DocumentId document, std::string_view element, std::string_view name, bool on);
    bool set_attribute(DocumentId document, std::string_view element, std::string_view name, const asset::Json& value);
    bool set_style(DocumentId document, std::string_view element, std::string_view property, std::string_view value);
    // Text boxes, selects and text areas: a string; ranges: a number; checkboxes and radios: true / false; anything
    // else: its content (markup).
    [[nodiscard]] std::optional<asset::Json> value(DocumentId document, std::string_view element) const;
    bool set_value(DocumentId document, std::string_view element, const asset::Json& value);
    bool focus(DocumentId document, std::string_view element);
    bool click(DocumentId document, std::string_view element);

    // What happened since the last call, in order.
    [[nodiscard]] std::vector<UiEvent> take_events();

    // For the editor's UI builder (part 6d). The element under a point on the screen (pixels): its document and its path
    // from <body> (the indices of elements at each level, as page_source.h counts them); nothing over empty space.
    struct ElementAt {
        DocumentId document;
        std::vector<int> path;
    };
    [[nodiscard]] std::optional<ElementAt> element_at(float x, float y) const;
    // Where an element's box (its border) is on the screen: x, y, width, height in pixels.
    [[nodiscard]] std::optional<std::array<float, 4>> element_box(DocumentId document, const std::vector<int>& path) const;

    // Where the UI is drawn: the game's picture on the screen, in pixels (render::Renderer::display_layout()).
    void set_viewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);

    // The mouse, in screen pixels; buttons 0 left, 1 right, 2 middle. Keyboard and gamepads come with parts 6b and 6e.
    void mouse_move(float x, float y);
    void mouse_button(int button, bool down);
    void mouse_wheel(float lines);  // positive: down
    // True when the mouse is over a visible document's element (so a game can leave clicks to the UI).
    [[nodiscard]] bool mouse_over_ui() const;
    // The keyboard: a key pressed (auto-repeats too) or let go, and typed text (UTF-8).
    void key(Key key, bool down, Modifiers modifiers = {});
    void text(std::string_view utf8);
    // A text box has focus: the host turns typed text on (platform::set_text_input).
    [[nodiscard]] bool wants_text() const;

    // Runs layout, transitions and animations. Call once a frame before draw().
    void update();
    // This frame's geometry (empty when no document is visible). Valid until the next draw().
    [[nodiscard]] render::UiFrame draw();

    [[nodiscard]] UiStats stats() const;

private:
    struct Impl;
    explicit Ui(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace noor::ui
