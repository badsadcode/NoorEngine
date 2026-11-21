// A `.noorui` page as text the UI builder edits (page_source.h). The reader is tolerant XML: elements, attributes in
// double or single quotes (or none), self-closing tags, void tags written without the slash (input, img, br, hr),
// comments, entities. It reads only <body>: the head and its style sheet are left exactly as they are.
#include "noor/ui/page_source.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <set>

namespace noor::ui {

using asset::Json;

namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool name_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ':' || c == '.'; }
std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
bool is_void(std::string_view tag) {
    const std::string t = lower(tag);
    return t == "input" || t == "img" || t == "br" || t == "hr" || t == "link" || t == "meta";
}

std::string unescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        const auto end = s.find(';', i);
        if (end == std::string_view::npos || end - i > 8) {
            out += s[i];
            continue;
        }
        const std::string_view name = s.substr(i + 1, end - i - 1);
        if (name == "lt") out += '<';
        else if (name == "gt") out += '>';
        else if (name == "amp") out += '&';
        else if (name == "quot") out += '"';
        else if (name == "apos" || name == "#39") out += '\'';
        else if (name == "nbsp") out += "\xC2\xA0";
        else {
            out += s.substr(i, end - i + 1);
        }
        i = end;
    }
    return out;
}
std::string escape_text(std::string_view s) {
    std::string out;
    for (const char c : s) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else out += c;
    }
    return out;
}
std::string escape_attribute(std::string_view s) {
    std::string out;
    for (const char c : s) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    return out;
}
std::string fold(std::string_view s) {  // whitespace runs to one space, trimmed
    std::string out;
    bool space = false;
    for (const char c : s) {
        if (is_space(c)) {
            space = !out.empty();
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

int line_of(std::string_view s, std::size_t pos) {
    return 1 + static_cast<int>(std::count(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(std::min(pos, s.size())), '\n'));
}

struct Reader {
    std::string_view s;
    std::size_t i = 0;

    Error error(std::size_t at, std::string what) const {
        return Error{ErrorCode::ParseError, std::format("line {}: {}", line_of(s, at), what)};
    }
    bool at(std::string_view token) const { return s.substr(i, token.size()) == token; }
    void skip_comment() {
        const auto end = s.find("-->", i + 4);
        i = end == std::string_view::npos ? s.size() : end + 3;
    }

    Result<PageElement> element() {
        PageElement e;
        e.open_begin = i;
        ++i;  // '<'
        const std::size_t name_begin = i;
        while (i < s.size() && name_char(s[i])) ++i;
        e.tag = std::string(s.substr(name_begin, i - name_begin));
        if (e.tag.empty()) return std::unexpected(error(e.open_begin, "a '<' that doesn't start a tag"));
        // Attributes, up to '>' or '/>'.
        for (;;) {
            const std::size_t before = i;
            while (i < s.size() && is_space(s[i])) ++i;
            if (i >= s.size()) return std::unexpected(error(e.open_begin, std::format("<{}> isn't finished", e.tag)));
            if (at("/>")) {
                i += 2;
                e.self_closing = true;
                break;
            }
            if (s[i] == '>') {
                ++i;
                break;
            }
            PageElement::Attribute a;
            a.begin = before;
            const std::size_t nb = i;
            while (i < s.size() && name_char(s[i])) ++i;
            if (i == nb) return std::unexpected(error(i, std::format("an unexpected '{}' in <{}>", s[i], e.tag)));
            a.name = std::string(s.substr(nb, i - nb));
            std::size_t j = i;
            while (j < s.size() && is_space(s[j])) ++j;
            if (j < s.size() && s[j] == '=') {
                i = j + 1;
                while (i < s.size() && is_space(s[i])) ++i;
                if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
                    const char q = s[i];
                    a.value_begin = i + 1;
                    const auto close = s.find(q, a.value_begin);
                    if (close == std::string_view::npos)
                        return std::unexpected(error(i, std::format("the value of {} in <{}> has no closing quote", a.name, e.tag)));
                    a.value_end = close;
                    i = close + 1;
                } else {  // unquoted
                    a.value_begin = i;
                    while (i < s.size() && !is_space(s[i]) && s[i] != '>' && !at("/>")) ++i;
                    a.value_end = i;
                }
                a.value = unescape(s.substr(a.value_begin, a.value_end - a.value_begin));
            } else {
                a.value_begin = a.value_end = i;  // a bare attribute ("checked")
            }
            a.end = i;
            e.attributes.push_back(std::move(a));
        }
        e.open_end = i;
        if (e.self_closing || is_void(e.tag)) {
            e.inner_begin = e.inner_end = e.close_begin = e.close_end = e.open_end;
            return e;
        }
        e.inner_begin = i;
        std::string raw_text;
        while (i < s.size()) {
            if (at("<!--")) {
                skip_comment();
            } else if (at("</")) {
                e.inner_end = e.close_begin = i;
                i += 2;
                const std::size_t nb = i;
                while (i < s.size() && name_char(s[i])) ++i;
                const std::string name(s.substr(nb, i - nb));
                const auto gt = s.find('>', i);
                if (gt == std::string_view::npos) return std::unexpected(error(e.close_begin, std::format("</{}> isn't finished", name)));
                if (lower(name) != lower(e.tag))
                    return std::unexpected(error(e.close_begin, std::format("</{}> closes <{}> (line {})", name, e.tag, line_of(s, e.open_begin))));
                i = gt + 1;
                e.close_end = i;
                e.text = fold(unescape(raw_text));
                return e;
            } else if (s[i] == '<') {
                auto child = element();
                if (!child) return child;
                e.children.push_back(std::move(*child));
                raw_text += ' ';
            } else {
                raw_text += s[i++];
            }
        }
        return std::unexpected(error(e.open_begin, std::format("<{}> is never closed", e.tag)));
    }
};

std::size_t line_start(std::string_view s, std::size_t pos) {
    while (pos > 0 && s[pos - 1] != '\n') --pos;
    return pos;
}
std::size_t line_end(std::string_view s, std::size_t pos) {  // the '\n' (or the end)
    while (pos < s.size() && s[pos] != '\n') ++pos;
    return pos;
}
bool only_space(std::string_view s, std::size_t a, std::size_t b) {
    for (std::size_t k = a; k < b; ++k)
        if (!is_space(s[k])) return false;
    return true;
}
std::string indent_of(std::string_view s, std::size_t pos) {
    const std::size_t ls = line_start(s, pos);
    std::size_t k = ls;
    while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
    return std::string(s.substr(ls, k - ls));
}

Result<ElementPath> to_path(const Json& j) {
    ElementPath p;
    if (j.is_null()) return p;
    if (!j.is_array()) return fail(ErrorCode::InvalidArgument, "an element's path is a list of numbers ([] for the body)");
    for (const auto& v : j) {
        if (!v.is_number_integer() || v.get<int>() < 0) return fail(ErrorCode::InvalidArgument, "an element's path is a list of numbers ([] for the body)");
        p.push_back(v.get<int>());
    }
    return p;
}

void collect_ids(const PageElement& e, std::set<std::string>& out) {
    if (const auto* id = e.attribute("id")) out.insert(id->value);
    for (const auto& c : e.children) collect_ids(c, out);
}

std::string replace(std::string_view s, std::size_t a, std::size_t b, std::string_view with) {
    std::string out(s.substr(0, a));
    out += with;
    out += s.substr(b);
    return out;
}

std::string set_attribute_text(std::string_view s, const PageElement& e, std::string_view name, const Json& value) {
    if (const auto* a = e.attribute(name)) {
        if (value.is_null() || (value.is_boolean() && !value.get<bool>())) return replace(s, a->begin, a->end, "");
        const std::string v = value.is_string() ? value.get<std::string>() : value.is_boolean() ? std::string(a->name) : value.dump();
        const bool quoted = a->value_begin > 0 && (s[a->value_begin - 1] == '"' || s[a->value_begin - 1] == '\'');
        if (a->value_begin == a->value_end && !quoted)  // a bare attribute gets a value
            return replace(s, a->end, a->end, std::format("=\"{}\"", escape_attribute(v)));
        if (quoted && s[a->value_begin - 1] == '"') return replace(s, a->value_begin, a->value_end, escape_attribute(v));
        return replace(s, a->value_begin - (quoted ? 1 : 0), a->value_end + (quoted ? 1 : 0), "\"" + escape_attribute(v) + "\"");
    }
    if (value.is_null() || (value.is_boolean() && !value.get<bool>())) return std::string(s);
    const std::string v = value.is_string() ? value.get<std::string>() : value.is_boolean() ? std::string(name) : value.dump();
    // Before the end of the opening tag ('>' or '/>').
    std::size_t at = e.open_end - 1;
    if (e.self_closing) at = s.rfind('/', e.open_end - 1);
    while (at > e.open_begin && is_space(s[at - 1])) --at;
    return replace(s, at, at, std::format(" {}=\"{}\"", name, escape_attribute(v)));
}

// A style attribute's declarations, in order.
std::vector<std::pair<std::string, std::string>> declarations(std::string_view style) {
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t k = 0;
    while (k < style.size()) {
        auto semi = style.find(';', k);
        if (semi == std::string_view::npos) semi = style.size();
        const std::string_view d = style.substr(k, semi - k);
        const auto colon = d.find(':');
        if (colon != std::string_view::npos) {
            const std::string p = fold(d.substr(0, colon)), v = fold(d.substr(colon + 1));
            if (!p.empty()) out.emplace_back(lower(p), v);
        }
        k = semi + 1;
    }
    return out;
}

struct Widget {
    const char* name;
    const char* label;
    const char* markup;  // {id} is the new element's id
};
constexpr Widget kWidgets[] = {
    {"panel", "Panel", R"(<div id="{id}" class="noor-panel"></div>)"},
    {"title", "Title", R"(<h1 id="{id}">Title</h1>)"},
    {"label", "Label", R"(<p id="{id}">Text</p>)"},
    {"button", "Button", R"(<div id="{id}" class="noor-button">Button</div>)"},
    {"image", "Image", R"(<img id="{id}" src="" style="width: 64dp; height: 64dp;"/>)"},
    {"slider", "Slider", R"(<input id="{id}" type="range" min="0" max="1" step="0.05" value="0.5"/>)"},
    {"checkbox", "Checkbox", R"(<input id="{id}" type="checkbox"/>)"},
    {"textbox", "Text box", R"(<input id="{id}" type="text" value=""/>)"},
};

constexpr const char* kHead = R"(<rml>
<head>
    <title>{title}</title>
    <style>
{style}    </style>
</head>
)";

struct Template {
    const char* name;
    const char* label;
    const char* style;
    const char* body;
};
constexpr Template kTemplates[] = {
    {"blank", "Blank page",
     R"(        body { width: 100%; height: 100%; }
)",
     R"(<body>
</body>
</rml>
)"},
    {"pause", "Pause menu",
     R"(        body { width: 100%; height: 100%; display: flex; justify-content: center; align-items: center;
               background-color: #0000006E; }
        #menu { width: 360dp; background-color: #FFFFFF26; backdrop-filter: blur(12dp); border: 2dp #FFFFFF40; }
        #title { color: #FFC857; text-align: center; }
)",
     R"(<body>
    <div id="menu" class="noor-panel">
        <h1 id="title">Paused</h1>
        <div id="resume" class="noor-button">Resume</div>
        <div id="settings" class="noor-button">Settings</div>
        <div id="quit" class="noor-button">Quit</div>
    </div>
</body>
</rml>
)"},
    {"hud", "HUD",
     R"(        body { width: 100%; height: 100%; }
        #score { position: absolute; right: 24dp; top: 24dp; font-size: 36dp; color: #FFC857; }
        #health { position: absolute; left: 24dp; bottom: 24dp; width: 240dp; height: 16dp; background-color: #00000080;
                  border-radius: 8dp; }
        #health-fill { width: 75%; height: 100%; border-radius: 8dp; decorator: linear-gradient(90deg, #E76F51, #FFC857); }
)",
     R"(<body>
    <div id="score">0</div>
    <div id="health"><div id="health-fill"></div></div>
</body>
</rml>
)"},
    {"settings", "Settings",
     R"(        body { width: 100%; height: 100%; display: flex; justify-content: center; align-items: center;
               background-color: #0000008C; }
        #settings { width: 420dp; }
        label { display: block; margin-top: 12dp; color: #B8B0CC; }
)",
     R"(<body>
    <div id="settings" class="noor-panel">
        <h1 id="title">Settings</h1>
        <label>Your name</label>
        <input id="name" type="text" value=""/>
        <label>Volume</label>
        <input id="volume" type="range" min="0" max="1" step="0.05" value="0.8"/>
        <label><input id="subtitles" type="checkbox"/> Subtitles</label>
        <div id="back" class="noor-button">Back</div>
    </div>
</body>
</rml>
)"},
};

} // namespace

const PageElement::Attribute* PageElement::attribute(std::string_view name) const {
    for (const auto& a : attributes)
        if (lower(a.name) == lower(name)) return &a;
    return nullptr;
}

Result<PageElement> parse_page(std::string_view text) {
    Reader r{text};
    while (r.i < text.size()) {
        if (r.at("<!--")) {
            r.skip_comment();
            continue;
        }
        // A style sheet or script is text, whatever it says ("<body>" in a CSS comment isn't the body).
        for (const std::string_view raw : {"style", "script"}) {
            if (text[r.i] == '<' && lower(text.substr(r.i + 1, raw.size())) == raw) {
                const auto close = lower(text).find(std::format("</{}", raw), r.i);
                r.i = close == std::string::npos ? text.size() : close;
                break;
            }
        }
        if (r.i >= text.size()) break;
        if (text[r.i] == '<' && lower(text.substr(r.i + 1, 4)) == "body" && r.i + 5 < text.size() &&
            (is_space(text[r.i + 5]) || text[r.i + 5] == '>' || text[r.i + 5] == '/'))
            return r.element();
        ++r.i;
    }
    return fail(ErrorCode::ParseError, "the page has no <body>");
}

const PageElement* find_element(const PageElement& body, const ElementPath& path) {
    const PageElement* e = &body;
    for (const int i : path) {
        if (i < 0 || static_cast<std::size_t>(i) >= e->children.size()) return nullptr;
        e = &e->children[static_cast<std::size_t>(i)];
    }
    return e;
}

namespace {
Json tree_of(const PageElement& e, ElementPath& path) {
    Json attrs = Json::object();
    for (const auto& a : e.attributes)
        if (a.name != "id" && a.name != "class" && a.name != "style") attrs[a.name] = a.value;
    Json j = {{"path", path}, {"tag", e.tag}, {"id", e.attribute("id") ? e.attribute("id")->value : ""},
              {"class", e.attribute("class") ? e.attribute("class")->value : ""},
              {"style", e.attribute("style") ? e.attribute("style")->value : ""}, {"text", e.text}, {"attributes", std::move(attrs)}};
    Json children = Json::array();
    for (std::size_t i = 0; i < e.children.size(); ++i) {
        path.push_back(static_cast<int>(i));
        children.push_back(tree_of(e.children[i], path));
        path.pop_back();
    }
    j["children"] = std::move(children);
    return j;
}
} // namespace

Json page_tree(const PageElement& body) {
    ElementPath path;
    return tree_of(body, path);
}

Result<PageEdit> edit_page(std::string_view text, const Json& edit) {
    if (!edit.is_object() || !edit.contains("op") || !edit["op"].is_string()) return fail(ErrorCode::InvalidArgument, "an edit needs an \"op\"");
    const std::string op = edit["op"];
    auto body = parse_page(text);
    if (!body) return std::unexpected(body.error());
    auto path = to_path(edit.value("path", Json()));
    if (!path) return std::unexpected(path.error());
    const PageElement* e = find_element(*body, *path);
    if (!e) return fail(ErrorCode::NotFound, "there's no element at that path (the page changed?)");
    PageEdit out;

    if (op == "set_attribute") {
        const std::string name = edit.value("name", std::string());
        if (name.empty() || !std::all_of(name.begin(), name.end(), name_char)) return fail(ErrorCode::InvalidArgument, "which attribute?");
        out.text = set_attribute_text(text, *e, name, edit.value("value", Json()));
    } else if (op == "set_text") {
        if (!e->children.empty()) return fail(ErrorCode::InvalidArgument, std::format("<{}> has elements inside: change their text instead", e->tag));
        const std::string t = escape_text(edit.value("text", std::string()));
        if (is_void(e->tag) && !e->self_closing) return fail(ErrorCode::InvalidArgument, std::format("<{}> has no text", e->tag));
        if (e->self_closing) {
            if (is_void(e->tag)) return fail(ErrorCode::InvalidArgument, std::format("<{}> has no text", e->tag));
            std::size_t slash = text.rfind('/', e->open_end - 1);
            out.text = replace(text, slash, e->open_end, std::format(">{}</{}>", t, e->tag));
        } else {
            out.text = replace(text, e->inner_begin, e->inner_end, t);
        }
    } else if (op == "set_style") {
        const std::string property = lower(fold(edit.value("property", std::string())));
        if (property.empty()) return fail(ErrorCode::InvalidArgument, "which style property?");
        const std::string value = fold(edit.value("value", std::string()));
        auto decls = declarations(e->attribute("style") ? e->attribute("style")->value : "");
        const auto it = std::find_if(decls.begin(), decls.end(), [&](const auto& d) { return d.first == property; });
        if (value.empty()) {
            if (it != decls.end()) decls.erase(it);
        } else if (it != decls.end()) {
            it->second = value;
        } else {
            decls.emplace_back(property, value);
        }
        std::string style;
        for (const auto& [p, v] : decls) style += (style.empty() ? "" : " ") + p + ": " + v + ";";
        out.text = set_attribute_text(text, *e, "style", style.empty() ? Json() : Json(style));
    } else if (op == "insert") {
        std::string markup;
        if (edit.contains("widget")) {
            const std::string w = edit.value("widget", std::string());
            const auto* widget = std::find_if(std::begin(kWidgets), std::end(kWidgets), [&](const Widget& x) { return w == x.name; });
            if (widget == std::end(kWidgets)) return fail(ErrorCode::InvalidArgument, std::format("no widget called \"{}\"", w));
            std::set<std::string> ids;
            collect_ids(*body, ids);
            std::string id = widget->name;
            for (int n = 2; ids.contains(id); ++n) id = std::format("{}-{}", widget->name, n);
            markup = widget->markup;
            markup.replace(markup.find("{id}"), 4, id);
        } else {
            markup = edit.value("markup", std::string());
            if (markup.empty()) return fail(ErrorCode::InvalidArgument, "insert what? (a \"widget\" or \"markup\")");
            if (auto check = parse_page("<body>" + markup + "</body>"); !check) return std::unexpected(check.error());
        }
        if (is_void(e->tag)) return fail(ErrorCode::InvalidArgument, std::format("<{}> can't hold other elements", e->tag));
        const int count = static_cast<int>(e->children.size());
        int index = edit.value("index", -1);
        if (index < 0 || index > count) index = count;
        out.inserted = *path;
        out.inserted.push_back(index);
        const std::string indent = indent_of(text, e->open_begin);
        if (e->self_closing) {
            const std::size_t slash = text.rfind('/', e->open_end - 1);
            out.text = replace(text, slash, e->open_end, std::format(">\n{}    {}\n{}</{}>", indent, markup, indent, e->tag));
        } else if (index < count) {
            const auto& next = e->children[static_cast<std::size_t>(index)];
            if (only_space(text, line_start(text, next.open_begin), next.open_begin))
                out.text = replace(text, next.open_begin, next.open_begin, markup + "\n" + indent_of(text, next.open_begin));
            else
                out.text = replace(text, next.open_begin, next.open_begin, markup);
        } else {
            const std::size_t ls = line_start(text, e->close_begin);
            if (only_space(text, ls, e->close_begin) && ls > e->inner_begin)
                out.text = replace(text, ls, ls, indent_of(text, e->close_begin) + "    " + markup + "\n");
            else if (count == 0 && only_space(text, e->inner_begin, e->inner_end))
                out.text = replace(text, e->inner_begin, e->inner_end, std::format("\n{}    {}\n{}", indent, markup, indent));
            else
                out.text = replace(text, e->close_begin, e->close_begin, markup);
        }
    } else if (op == "remove") {
        if (path->empty()) return fail(ErrorCode::InvalidArgument, "the body itself can't be removed");
        std::size_t a = e->open_begin, b = e->close_end;
        if (only_space(text, line_start(text, a), a) && only_space(text, b, line_end(text, b))) {
            a = line_start(text, a);
            b = std::min(text.size(), line_end(text, b) + 1);
        }
        out.text = replace(text, a, b, "");
    } else if (op == "move") {
        if (path->empty()) return fail(ErrorCode::InvalidArgument, "the body itself can't move");
        const int by = edit.value("by", 0);
        if (by != -1 && by != 1) return fail(ErrorCode::InvalidArgument, "move by -1 (up) or 1 (down)");
        ElementPath parent_path(path->begin(), path->end() - 1);
        const PageElement* parent = find_element(*body, parent_path);
        const int index = path->back(), other = index + by;
        if (other < 0 || other >= static_cast<int>(parent->children.size()))
            return fail(ErrorCode::InvalidArgument, by < 0 ? "it's first already" : "it's last already");
        const auto& x = parent->children[static_cast<std::size_t>(std::min(index, other))];
        const auto& y = parent->children[static_cast<std::size_t>(std::max(index, other))];
        out.text = std::string(text.substr(0, x.open_begin));
        out.text += text.substr(y.open_begin, y.close_end - y.open_begin);
        out.text += text.substr(x.close_end, y.open_begin - x.close_end);
        out.text += text.substr(x.open_begin, x.close_end - x.open_begin);
        out.text += text.substr(y.close_end);
        out.inserted = parent_path;
        out.inserted.push_back(other);
    } else {
        return fail(ErrorCode::InvalidArgument, std::format("no edit called \"{}\"", op));
    }
    // What comes out must still read (a mistake here must never leave a broken page).
    if (auto check = parse_page(out.text); !check)
        return fail(ErrorCode::Unknown, std::format("the edit would break the page ({}); nothing was changed", check.error().message));
    return out;
}

Json page_palette() {
    Json out = Json::array();
    for (const auto& w : kWidgets) out.push_back({{"name", w.name}, {"label", w.label}, {"markup", w.markup}});
    return out;
}

Json page_templates() {
    Json out = Json::array();
    for (const auto& t : kTemplates) out.push_back({{"name", t.name}, {"label", t.label}});
    return out;
}

Result<std::string> page_template(std::string_view name, std::string_view title) {
    const auto* t = std::find_if(std::begin(kTemplates), std::end(kTemplates), [&](const Template& x) { return name == x.name; });
    if (t == std::end(kTemplates)) return fail(ErrorCode::InvalidArgument, std::format("no page template called \"{}\"", name));
    std::string head = kHead;
    head.replace(head.find("{title}"), 7, escape_text(title));
    head.replace(head.find("{style}"), 7, t->style);
    return head + t->body;
}

} // namespace noor::ui
