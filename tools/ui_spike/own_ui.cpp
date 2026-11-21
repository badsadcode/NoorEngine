// Spike A (ADR-041): the pause menu with our own retained widgets, drawn through the HUD we have (SDF text, rectangles).
// A deliberately small slice of what a real version would be: panels that stack their children, labels, buttons, focus
// moved by up/down (keyboard or gamepad) and activated by Enter, a theme of colours. Read from a JSON layout, the way a
// `.noorui` file and an editor would write it.
#include "spike.h"

#include "noor/game/hud.h"

#include <chrono>

namespace ui_spike {

using noor::asset::Json;
using noor::math::Vec2;
using noor::math::Vec4;
using noor::runtime::UiAnchor;
using noor::runtime::UiCommand;

namespace {

struct Node {
    enum class Kind { Panel, Label, Button } kind = Kind::Panel;
    std::string id, text;
    float size = 32, padding = 0, spacing = 0, width = 0;
    Vec4 background{0, 0, 0, 0};
    std::vector<Node> children;
    // Laid out (UI units, top-left).
    float x = 0, y = 0, w = 0, h = 0;
};

Vec4 colour(const Json& j, const char* key, Vec4 fallback) {
    if (!j.contains(key)) return fallback;
    const auto c = noor::runtime::api::parse_color(j[key].get<std::string>());
    return c ? *c : fallback;
}

Node parse(const Json& j) {
    Node n;
    if (j.contains("panel")) {
        const Json& p = j["panel"];
        n.kind = Node::Kind::Panel;
        n.padding = p.value("padding", 0.0f);
        n.spacing = p.value("spacing", 0.0f);
        n.width = p.value("width", 0.0f);
        n.background = colour(p, "background", n.background);
        for (const auto& c : p.value("children", Json::array())) n.children.push_back(parse(c));
    } else if (j.contains("label")) {
        n.kind = Node::Kind::Label;
        n.text = j["label"];
        n.size = j.value("size", 32.0f);
    } else {
        n.kind = Node::Kind::Button;
        n.text = j["button"];
        n.id = j.value("id", n.text);
        n.size = j.value("size", 32.0f);
    }
    return n;
}

// Sizes from the content up: labels and buttons from their text, panels from their children.
void measure(Node& n, const noor::game::Hud& hud) {
    switch (n.kind) {
    case Node::Kind::Label: {
        const Vec2 s = hud.measure(n.text, n.size);
        n.w = s.x, n.h = s.y;
        break;
    }
    case Node::Kind::Button: {
        const Vec2 s = hud.measure(n.text, n.size);
        n.w = s.x + 48, n.h = s.y + 24;
        break;
    }
    case Node::Kind::Panel: {
        float w = 0, h = 0;
        for (auto& c : n.children) {
            measure(c, hud);
            w = std::max(w, c.w);
            h += c.h;
        }
        h += n.spacing * static_cast<float>(std::max<std::size_t>(1, n.children.size()) - 1);
        n.w = std::max(n.width, w + 2 * n.padding);
        n.h = h + 2 * n.padding;
        break;
    }
    }
}

// Positions from the top down: children stacked, centred; buttons stretched to the panel's inner width.
void place(Node& n, float x, float y) {
    n.x = x, n.y = y;
    if (n.kind != Node::Kind::Panel) return;
    float cy = y + n.padding;
    for (auto& c : n.children) {
        if (c.kind == Node::Kind::Button) c.w = n.w - 2 * n.padding;
        place(c, x + (n.w - c.w) / 2, cy);
        cy += c.h + n.spacing;
    }
}

void buttons(Node& n, std::vector<Node*>& out) {
    if (n.kind == Node::Kind::Button) out.push_back(&n);
    for (auto& c : n.children) buttons(c, out);
}

// The same colours as pause.rml's, as hex (sRGB, as colour pickers show them; the HUD wants linear: parse_color).
Vec4 hex(const char* s) { return *noor::runtime::api::parse_color(s); }
struct Theme {
    Vec4 dim = hex("#0000008C"), button = hex("#2A2140FF"), focus = hex("#4B1AF2FF"), text = hex("#EDE8F7FF");
    Vec4 title = hex("#FFC857FF");
};

UiCommand rect(float x, float y, float w, float h, Vec4 c) {
    UiCommand r;
    r.kind = UiCommand::Kind::Rect;
    r.position = {x, y};
    r.size = {w, h};
    r.color = c;
    return r;
}
UiCommand text(const std::string& s, float x, float y, float size, Vec4 c) {
    UiCommand t;
    t.kind = UiCommand::Kind::Text;
    t.text = s;
    t.position = {x, y};
    t.text_size = size;
    t.color = c;
    return t;
}

void draw(const Node& n, const Node* focused, const Theme& th, const noor::game::Hud& hud, std::vector<UiCommand>& out) {
    switch (n.kind) {
    case Node::Kind::Panel:
        if (n.background.w > 0) out.push_back(rect(n.x, n.y, n.w, n.h, n.background));
        break;
    case Node::Kind::Label:
        out.push_back(text(n.text, n.x, n.y, n.size, th.title));
        break;
    case Node::Kind::Button: {
        const bool f = &n == focused;
        out.push_back(rect(n.x, n.y, n.w, n.h, f ? th.focus : th.button));
        if (f) {  // a light frame round the focused one: visible without colour too
            const Vec4 edge{1, 1, 1, 0.9f};
            out.push_back(rect(n.x, n.y, n.w, 3, edge));
            out.push_back(rect(n.x, n.y + n.h - 3, n.w, 3, edge));
            out.push_back(rect(n.x, n.y, 3, n.h, edge));
            out.push_back(rect(n.x + n.w - 3, n.y, 3, n.h, edge));
        }
        const Vec2 s = hud.measure(n.text, n.size);
        out.push_back(text(n.text, n.x + (n.w - s.x) / 2, n.y + (n.h - s.y) / 2, n.size, th.text));
        break;
    }
    }
    for (const auto& c : n.children) draw(c, focused, th, hud, out);
}

} // namespace

// The pause menu as our own widgets: JSON in, the menu laid out round the screen's centre, focus on button `focus`.
SpikeResult own_pause_menu(const std::string& layout_json, int focus, int width, int height) {
    static const noor::game::Hud hud;  // its font atlas (baked once: the game does that too)
    SpikeResult r;
    const auto t0 = std::chrono::steady_clock::now();
    constexpr int kRuns = 1000;
    std::vector<noor::render::OverlayVertex> verts;
    for (int run = 0; run < kRuns; ++run) {
        // Every frame from the file's JSON: the worst case (a real one keeps the tree and re-lays it only when it changes).
        Node root = parse(Json::parse(layout_json));
        measure(root, hud);
        const float screen_w = 720.0f * width / height;
        place(root, (screen_w - root.w) / 2, (720.0f - root.h) / 2);
        std::vector<Node*> focusable;
        buttons(root, focusable);
        const Node* focused = focusable.empty() ? nullptr : focusable[static_cast<std::size_t>(std::clamp(focus, 0, static_cast<int>(focusable.size()) - 1))];
        std::vector<UiCommand> cmds;
        cmds.push_back(rect(0, 0, screen_w, 720, Theme{}.dim));
        draw(root, focused, Theme{}, hud, cmds);
        verts.clear();
        hud.build(cmds, static_cast<float>(width), static_cast<float>(height), verts);
    }
    r.ms_per_frame = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / kRuns;
    r.triangles = verts.size() / 3;
    // Draw it.
    Texture atlas;
    atlas.width = static_cast<int>(hud.atlas_width());
    atlas.height = static_cast<int>(hud.atlas_height());
    atlas.channels = 1;
    atlas.sdf = true;
    atlas.pixels.assign(hud.atlas().begin(), hud.atlas().end());
    r.draw = [verts, atlas](Image& img) {
        const std::array<int, 4> all{0, 0, img.width, img.height};
        for (std::size_t i = 0; i + 2 < verts.size(); i += 3) {
            Vert v[3];
            bool textured = verts[i].uv.x >= 0;
            for (int k = 0; k < 3; ++k) {
                const auto& o = verts[i + k];
                v[k] = {o.position.x, o.position.y, o.uv.x, o.uv.y, o.color.x, o.color.y, o.color.z, o.color.w};
            }
            img.triangle(v[0], v[1], v[2], textured ? &atlas : nullptr, true, false, all);
        }
    };
    return r;
}

} // namespace ui_spike
