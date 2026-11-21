#pragma once
// A `.noorui` page as text the editor's UI builder changes (gameplay foundations part 6d): the page's elements read from
// its markup with where each is in the text, and edits made as small changes to that text, so a hand-written page keeps
// its formatting, comments and style sheet. No RmlUi here: the UI builder works on files; RmlUi only shows them.
//
// Elements are addressed by a path from <body>: the indices of elements (text doesn't count) at each level, so [0, 2]
// is the third element in the body's first element. RmlUi's document counts the same way (Ui::element_at).

#include "noor/asset/json.h"
#include "noor/foundation/error.h"

#include <string>
#include <string_view>
#include <vector>

namespace noor::ui {

using ElementPath = std::vector<int>;

struct PageElement {
    std::string tag;
    struct Attribute {
        std::string name, value;
        std::size_t begin = 0, end = 0;              // the whole attribute, the space before it included
        std::size_t value_begin = 0, value_end = 0;  // between the quotes
    };
    std::vector<Attribute> attributes;
    std::size_t open_begin = 0, open_end = 0;    // `<tag ...>` (or `<tag .../>`)
    bool self_closing = false;
    std::size_t inner_begin = 0, inner_end = 0;  // between the tags
    std::size_t close_begin = 0, close_end = 0;  // `</tag>` (self-closing: the same as open_end)
    std::string text;                            // its own text (not its children's), whitespace folded
    std::vector<PageElement> children;           // elements only

    [[nodiscard]] const Attribute* attribute(std::string_view name) const;
};

// The page's <body>, read from its text. Fails with the line where the markup doesn't make sense.
[[nodiscard]] Result<PageElement> parse_page(std::string_view text);

// The element at `path` (nullptr if there's none).
[[nodiscard]] const PageElement* find_element(const PageElement& body, const ElementPath& path);

// The body's tree for the editor: {path, tag, id, class, style, text, children: [...]}, and its other attributes.
[[nodiscard]] asset::Json page_tree(const PageElement& body);

// An edit, as the editor sends it, applied to the page's text: the new text, or why not.
//   {"op": "set_attribute", "path", "name", "value"}   (value null: removed)
//   {"op": "set_text", "path", "text"}                 (the element's content becomes this text, escaped)
//   {"op": "set_style", "path", "property", "value"}   (in its style attribute; "" removes the property)
//   {"op": "insert", "path" (the parent), "index" (-1: last), "widget" | "markup"}
//   {"op": "remove", "path"}
//   {"op": "move", "path", "by" (-1: up, 1: down)}
// Inserted widgets get an id of their own ("button", "button-2"...). Returns the edited text and, for an insert, the new
// element's path (`inserted`).
struct PageEdit {
    std::string text;
    ElementPath inserted;
};
[[nodiscard]] Result<PageEdit> edit_page(std::string_view text, const asset::Json& edit);

// The widgets the builder offers ({name, label, markup}): panel, title, label, button, image, slider, checkbox, text box.
[[nodiscard]] asset::Json page_palette();
// The pages a new one can start from: blank, pause, hud, settings ({name, label}), and one's text.
[[nodiscard]] asset::Json page_templates();
[[nodiscard]] Result<std::string> page_template(std::string_view name, std::string_view title);

} // namespace noor::ui
