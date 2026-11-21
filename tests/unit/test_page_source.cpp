// The UI builder's page model (gameplay foundations part 6d, noor/ui/page_source.h): a page's elements read from its
// markup, and edits that change only their own part of the text (a hand-written page keeps its formatting, comments
// and head), give new widgets ids of their own, refuse what would break the page, and leave every template readable.
#include "noor/ui/page_source.h"

#include <ostream>
#include <doctest/doctest.h>

using namespace noor;
using asset::Json;

namespace {

constexpr const char* kPage = R"(<rml>
<head>
    <title>Menu</title>
    <style>
        body { width: 100%; }  /* a <body> in a comment of the style sheet isn't the body */
    </style>
</head>
<body class="dim">
    <!-- the menu -->
    <div id="menu" class="noor-panel" style="width: 360dp;">
        <h1 id="title">Paused</h1>
        <div id="resume" class='noor-button'>Resume</div>
        <img src="logo.png">
        <input id="volume" type="range" value="0.5"/>
        <p>Plain &amp; simple</p>
    </div>
</body>
</rml>
)";

std::string edited(std::string_view text, const Json& edit) {
    auto r = ui::edit_page(text, edit);
    REQUIRE_MESSAGE(r, (r ? "" : r.error().message));
    return r->text;
}

} // namespace

TEST_CASE("UI pages: elements read from the markup, with their paths") {
    auto body = ui::parse_page(kPage);
    REQUIRE(body);
    CHECK(body->tag == "body");
    REQUIRE(body->children.size() == 1);
    const auto& menu = body->children[0];
    CHECK(menu.attribute("id")->value == "menu");
    REQUIRE(menu.children.size() == 5);
    CHECK(menu.children[1].attribute("class")->value == "noor-button");  // single quotes
    CHECK(menu.children[2].tag == "img");                                // void, no slash
    CHECK(menu.children[3].self_closing);
    CHECK(menu.children[4].text == "Plain & simple");
    CHECK(ui::find_element(*body, {0, 1})->text == "Resume");
    CHECK(ui::find_element(*body, {0, 9}) == nullptr);

    const Json tree = ui::page_tree(*body);
    CHECK(tree["children"][0]["id"] == "menu");
    CHECK(tree["children"][0]["style"] == "width: 360dp;");
    CHECK(tree["children"][0]["children"][3]["attributes"]["type"] == "range");
    CHECK(tree["children"][0]["children"][1]["path"] == Json::array({0, 1}));

    CHECK_FALSE(ui::parse_page("<rml><head></head></rml>"));                 // no body
    const auto broken = ui::parse_page("<rml><body>\n<div><p>x</div></body></rml>");
    REQUIRE_FALSE(broken);
    CHECK(broken.error().message.find("line 2") != std::string::npos);      // where
}

TEST_CASE("UI pages: edits change only their part of the text") {
    // An attribute's value, in either quote style; a new attribute; one removed.
    std::string t = edited(kPage, {{"op", "set_attribute"}, {"path", {0, 1}}, {"name", "class"}, {"value", "noor-button big"}});
    CHECK(t.find(R"(<div id="resume" class="noor-button big">Resume</div>)") != std::string::npos);
    t = edited(t, {{"op", "set_attribute"}, {"path", {0, 0}}, {"name", "data-x"}, {"value", "1 < 2"}});
    CHECK(t.find(R"(<h1 id="title" data-x="1 &lt; 2">Paused</h1>)") != std::string::npos);
    t = edited(t, {{"op", "set_attribute"}, {"path", {0, 0}}, {"name", "data-x"}, {"value", nullptr}});
    CHECK(t.find(R"(<h1 id="title">Paused</h1>)") != std::string::npos);
    t = edited(t, {{"op", "set_attribute"}, {"path", {0, 3}}, {"name", "step"}, {"value", "0.1"}});
    CHECK(t.find(R"(<input id="volume" type="range" value="0.5" step="0.1"/>)") != std::string::npos);
    // Everything else exactly as it was: the head, the comment, the indentation.
    CHECK(t.find("/* a <body> in a comment of the style sheet isn't the body */") != std::string::npos);
    CHECK(t.find("    <!-- the menu -->\n") != std::string::npos);

    // Text: escaped; a self-closing element opened up for it.
    t = edited(kPage, {{"op", "set_text"}, {"path", {0, 0}}, {"text", "Paused <again> & again"}});
    CHECK(t.find("<h1 id=\"title\">Paused &lt;again&gt; &amp; again</h1>") != std::string::npos);
    CHECK_FALSE(ui::edit_page(kPage, {{"op", "set_text"}, {"path", {0}}, {"text", "x"}}));     // has elements inside
    CHECK_FALSE(ui::edit_page(kPage, {{"op", "set_text"}, {"path", {0, 3}}, {"text", "x"}}));  // an input has no text

    // Style properties, one at a time, the rest kept in order.
    t = edited(kPage, {{"op", "set_style"}, {"path", {0}}, {"property", "Color"}, {"value", "#FFC857"}});
    CHECK(t.find(R"(style="width: 360dp; color: #FFC857;")") != std::string::npos);
    t = edited(t, {{"op", "set_style"}, {"path", {0}}, {"property", "width"}, {"value", "400dp"}});
    CHECK(t.find(R"(style="width: 400dp; color: #FFC857;")") != std::string::npos);
    t = edited(t, {{"op", "set_style"}, {"path", {0}}, {"property", "width"}, {"value", ""}});
    t = edited(t, {{"op", "set_style"}, {"path", {0}}, {"property", "color"}, {"value", ""}});
    CHECK(t.find("style=") == std::string::npos);  // nothing left: the attribute goes
}

TEST_CASE("UI pages: widgets go in on their own lines with ids of their own, and out again") {
    // Appended to the menu: on a line of its own, indented like its siblings.
    auto r = ui::edit_page(kPage, {{"op", "insert"}, {"path", {0}}, {"widget", "button"}});
    REQUIRE(r);
    CHECK(r->text.find("        <p>Plain &amp; simple</p>\n        <div id=\"button\" class=\"noor-button\">Button</div>\n    </div>") !=
          std::string::npos);
    CHECK(r->inserted == ui::ElementPath{0, 5});
    // Another: a new id.
    auto r2 = ui::edit_page(r->text, {{"op", "insert"}, {"path", {0}}, {"index", 0}, {"widget", "button"}});
    REQUIRE(r2);
    CHECK(r2->text.find("<div id=\"button-2\" class=\"noor-button\">Button</div>\n        <h1 id=\"title\">") != std::string::npos);
    CHECK(r2->inserted == ui::ElementPath{0, 0});
    // Into an empty element.
    auto r3 = ui::edit_page("<rml><body>\n    <div id=\"a\"></div>\n</body></rml>", {{"op", "insert"}, {"path", {0}}, {"widget", "label"}});
    REQUIRE(r3);
    CHECK(r3->text == "<rml><body>\n    <div id=\"a\">\n        <p id=\"label\">Text</p>\n    </div>\n</body></rml>");
    // Markup of one's own; markup that doesn't read is refused.
    CHECK(ui::edit_page(kPage, {{"op", "insert"}, {"path", {}}, {"markup", "<p>Hi</p>"}}));
    CHECK_FALSE(ui::edit_page(kPage, {{"op", "insert"}, {"path", {}}, {"markup", "<p>Hi"}}));
    CHECK_FALSE(ui::edit_page(kPage, {{"op", "insert"}, {"path", {0, 3}}, {"widget", "label"}}));  // into an input
    CHECK_FALSE(ui::edit_page(kPage, {{"op", "insert"}, {"path", {}}, {"widget", "spaceship"}}));

    // Removed with its line; moved past a sibling (formatting kept).
    std::string t = edited(kPage, {{"op", "remove"}, {"path", {0, 2}}});
    CHECK(t.find("logo.png") == std::string::npos);
    CHECK(t.find("Resume</div>\n        <input id=\"volume\"") != std::string::npos);
    auto moved = ui::edit_page(kPage, {{"op", "move"}, {"path", {0, 1}}, {"by", -1}});
    REQUIRE(moved);
    CHECK(moved->text.find("<div id=\"resume\" class='noor-button'>Resume</div>\n        <h1 id=\"title\">Paused</h1>") != std::string::npos);
    CHECK(moved->inserted == ui::ElementPath{0, 0});
    CHECK_FALSE(ui::edit_page(kPage, {{"op", "move"}, {"path", {0, 0}}, {"by", -1}}));  // first already
    CHECK_FALSE(ui::edit_page(kPage, {{"op", "remove"}, {"path", {}}}));                // not the body
    CHECK_FALSE(ui::edit_page(kPage, {{"op", "remove"}, {"path", {7}}}));               // nothing there
}

TEST_CASE("UI pages: every template reads, and its palette's widgets go into it") {
    for (const auto& t : ui::page_templates()) {
        auto text = ui::page_template(t["name"].get<std::string>(), "Test & title");
        REQUIRE(text);
        CHECK(text->find("<title>Test &amp; title</title>") != std::string::npos);
        auto body = ui::parse_page(*text);
        REQUIRE_MESSAGE(body, t["name"].get<std::string>());
        std::string page = *text;
        for (const auto& w : ui::page_palette()) {
            auto r = ui::edit_page(page, {{"op", "insert"}, {"path", {}}, {"widget", w["name"]}});
            REQUIRE_MESSAGE(r, w["name"].get<std::string>());
            page = r->text;
        }
        CHECK(ui::parse_page(page)->children.size() == body->children.size() + ui::page_palette().size());
    }
    CHECK_FALSE(ui::page_template("castle", "x"));
}
