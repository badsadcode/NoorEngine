#pragma once
// A running game's UI documents (gameplay foundations part 6b, ADR-041): the World's runtime::UiDocuments, made of a Ui
// that starts the first time a document opens (a game without UI never starts RmlUi). It opens the scene's
// `ui_document` components, gives scripts their documents (runtime::api::ui) and sends what happens on elements to the
// World as messages from each document's emitter: "click" and "click#<element id>", with {type, element, value}.
//
// A host's frame:  ui.input(...)  ->  world.frame(...)  ->  ui.draw(viewport)
// Events from input reach scripts in the same frame; events from what scripts did (a script's click) in the next.

#include "noor/runtime/ui_documents.h"
#include "noor/runtime/world.h"
#include "noor/ui/ui.h"

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace noor::ui {

class WorldUi final : public runtime::UiDocuments {
public:
    explicit WorldUi(UiDesc desc);
    ~WorldUi() override;
    WorldUi(const WorldUi&) = delete;
    WorldUi& operator=(const WorldUi&) = delete;

    // Becomes the world's UI and opens the scene's ui_document components ({"document": path, "visible": true}), each
    // belonging to its entity. Problems are logged with the entity's name. Detaches in the destructor.
    void attach(runtime::World& world);
    // Lets go of the world (before it's destroyed): no more messages to it; the documents stay until this goes. The
    // editor's viewport calls it from another thread while no frame runs, so it touches the world and nothing else.
    void detach();
    // The UI, once something opened a document.
    [[nodiscard]] Ui* ui() noexcept { return ui_.get(); }

    struct Input {
        bool mouse = true;  // false: the mouse is the game's (captured) and the UI doesn't see it
        float mouse_x = 0, mouse_y = 0;
        std::array<bool, 3> pressed{}, released{};  // left, right, middle
        float wheel = 0;                            // lines, positive: down
        std::vector<std::pair<Key, bool>> keys;     // in order: down (repeats too) or up
        Modifiers modifiers;
        std::string text;
    };
    // Before the world's frame. Nothing happens until a document is open.
    void input(const Input& in);
    // After the world's frame: lays out and draws in the viewport (screen pixels). Empty without UI.
    [[nodiscard]] render::UiFrame draw(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
    // A text box has focus (the host turns typed text on).
    [[nodiscard]] bool wants_text() const { return ui_ && ui_->wants_text(); }

    // runtime::UiDocuments
    [[nodiscard]] Result<std::uint32_t> open(std::string_view path) override;
    void close(std::uint32_t document) override;
    [[nodiscard]] bool is_open(std::uint32_t document) const override;
    void set_visible(std::uint32_t document, bool visible) override;
    [[nodiscard]] bool visible(std::uint32_t document) const override;
    [[nodiscard]] std::optional<std::uint32_t> find(std::string_view path) const override;
    bool set_text(std::uint32_t document, std::string_view element, std::string_view text) override;
    bool set_rml(std::uint32_t document, std::string_view element, std::string_view rml) override;
    bool set_class(std::uint32_t document, std::string_view element, std::string_view name, bool on) override;
    bool set_attribute(std::uint32_t document, std::string_view element, std::string_view name, const asset::Json& value) override;
    bool set_style(std::uint32_t document, std::string_view element, std::string_view property, std::string_view value) override;
    [[nodiscard]] std::optional<asset::Json> value(std::uint32_t document, std::string_view element) const override;
    bool set_value(std::uint32_t document, std::string_view element, const asset::Json& value) override;
    bool focus(std::uint32_t document, std::string_view element) override;
    bool click(std::uint32_t document, std::string_view element) override;

private:
    void forward_events();  // the Ui's events -> the World's queued messages

    UiDesc desc_;
    std::unique_ptr<Ui> ui_;
    runtime::World* world_ = nullptr;
    std::int32_t x_ = 0, y_ = 0, width_ = 1280, height_ = 720;
};

} // namespace noor::ui
