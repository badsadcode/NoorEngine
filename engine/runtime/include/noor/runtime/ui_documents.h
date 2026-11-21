#pragma once
// Game UI documents as the World sees them (gameplay foundations part 6b, ADR-041). The runtime doesn't know RmlUi: a
// host (noor_player, through noor::ui::WorldUi) gives the World an implementation of this, and scripts reach it through
// runtime::api::ui (Lua's noor.ui.open and its documents, C#'s Ui.Open and UiDocument).
//
// Events from a document's elements reach scripts as messages (World::listen) from the document's emitter id
// (World::ui_emitter): a click on the element with id "resume" sends "click" and "click#resume", with data
// {"type": "click", "element": "resume", "value": ...}. Events: click, change, submit, focus, blur, mouseover,
// mouseout. They're queued and delivered at the start of the next frame (World::queue_message), so the order is the
// same in every run.

#include "noor/asset/json.h"
#include "noor/foundation/error.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace noor::runtime {

class UiDocuments {
public:
    virtual ~UiDocuments() = default;

    // Opens a document (a path inside the project, "UI/pause.noorui") and shows it: its id (from 1).
    [[nodiscard]] virtual Result<std::uint32_t> open(std::string_view path) = 0;
    virtual void close(std::uint32_t document) = 0;
    [[nodiscard]] virtual bool is_open(std::uint32_t document) const = 0;
    virtual void set_visible(std::uint32_t document, bool visible) = 0;
    [[nodiscard]] virtual bool visible(std::uint32_t document) const = 0;
    // An open document by the path it was opened with (the first, if it's open twice).
    [[nodiscard]] virtual std::optional<std::uint32_t> find(std::string_view path) const = 0;

    // An element by its id. Each is false (with nothing changed) when the document or the element isn't there.
    virtual bool set_text(std::uint32_t document, std::string_view element, std::string_view text) = 0;  // plain text
    virtual bool set_rml(std::uint32_t document, std::string_view element, std::string_view rml) = 0;     // markup
    virtual bool set_class(std::uint32_t document, std::string_view element, std::string_view name, bool on) = 0;
    // An attribute ("disabled", "src", "value"...); null removes it.
    virtual bool set_attribute(std::uint32_t document, std::string_view element, std::string_view name, const asset::Json& value) = 0;
    // A style property ("width", "color"...) as RCSS text; empty: back to the style sheet's.
    virtual bool set_style(std::uint32_t document, std::string_view element, std::string_view property, std::string_view value) = 0;
    // A form control's value: text boxes and selects a string, ranges a number, checkboxes and radios true / false;
    // other elements their text. Nothing when the element isn't there.
    [[nodiscard]] virtual std::optional<asset::Json> value(std::uint32_t document, std::string_view element) const = 0;
    virtual bool set_value(std::uint32_t document, std::string_view element, const asset::Json& value) = 0;
    virtual bool focus(std::uint32_t document, std::string_view element) = 0;
    // As if it were clicked (a menu driven by a test or a script): the click event follows next frame.
    virtual bool click(std::uint32_t document, std::string_view element) = 0;
};

} // namespace noor::runtime
