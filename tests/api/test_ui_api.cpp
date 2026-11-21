// The UI tab through the editor API (gameplay foundations, part 6d): pages listed and made from templates, a page's
// element tree, edits written straight to the file, undo and redo per page (an edit made in NoorCode meanwhile kept),
// and mistakes refused with reasons and nothing written.
#include <ostream>
#include <doctest/doctest.h>

#include "noor_editor_api.h"
#include "noor/asset/json.h"
#include "noor/foundation/file_io.h"
#include "noor/foundation/guid.h"

#include <filesystem>
#include <string>

namespace stdfs = std::filesystem;
using noor::asset::Json;

namespace {

struct UiDoc {
    stdfs::path dir = stdfs::temp_directory_path() / ("noor_ui_api_" + noor::Guid::generate().to_string());
    noor_doc* doc = noor_doc_create_project(dir.string().c_str(), "UiTest");
    UiDoc() { REQUIRE(doc != nullptr); }
    ~UiDoc() {
        if (doc) noor_doc_close(doc);
        std::error_code ec;
        stdfs::remove_all(dir, ec);
    }
    Json ui(const Json& command) const {
        const char* r = noor_doc_ui(doc, command.dump().c_str());
        REQUIRE_MESSAGE(r != nullptr, noor_last_error());
        return Json::parse(r);
    }
    bool fails(const Json& command) const { return noor_doc_ui(doc, command.dump().c_str()) == nullptr; }
    std::string file(const std::string& rel) const { return noor::fs::read_file(dir / rel).value_or(""); }
};

} // namespace

TEST_CASE("ui api: templates, a new page, its tree, edits to the file, undo and redo") {
    UiDoc d;
    const Json catalog = d.ui({{"op", "templates"}});
    CHECK(catalog["templates"].size() == 4);
    CHECK(catalog["palette"].size() == 8);
    CHECK(d.ui({{"op", "list"}})["pages"].empty());

    // A pause menu from its template.
    Json page = d.ui({{"op", "new"}, {"path", "UI/pause.noorui"}, {"template", "pause"}, {"title", "Pause"}});
    CHECK(page["path"] == "UI/pause.noorui");
    CHECK(page["tree"]["children"][0]["id"] == "menu");
    CHECK(page["tree"]["children"][0]["children"].size() == 4);
    CHECK_FALSE(page["can_undo"].get<bool>());
    CHECK(d.ui({{"op", "list"}})["pages"] == Json::array({"UI/pause.noorui"}));
    CHECK(d.fails({{"op", "new"}, {"path", "UI/pause.noorui"}}));  // there already

    // Build on it: a slider in the menu, its id, the title's text and colour.
    page = d.ui({{"op", "edit"}, {"path", "UI/pause.noorui"}, {"edit", {{"op", "insert"}, {"path", {0}}, {"index", 1}, {"widget", "slider"}}}});
    CHECK(page["selected"] == Json::array({0, 1}));
    CHECK(page["tree"]["children"][0]["children"][1]["id"] == "slider");
    page = d.ui({{"op", "edit"}, {"path", "UI/pause.noorui"}, {"edit", {{"op", "set_attribute"}, {"path", {0, 1}}, {"name", "id"}, {"value", "volume"}}}});
    page = d.ui({{"op", "edit"}, {"path", "UI/pause.noorui"}, {"edit", {{"op", "set_text"}, {"path", {0, 0}}, {"text", "Take a break"}}}});
    page = d.ui({{"op", "edit"}, {"path", "UI/pause.noorui"}, {"edit", {{"op", "set_style"}, {"path", {0, 0}}, {"property", "color"}, {"value", "#7BD88F"}}}});
    CHECK(page["can_undo"].get<bool>());
    const std::string built = d.file("UI/pause.noorui");
    CHECK(built.find(R"(<input id="volume" type="range")") != std::string::npos);
    CHECK(built.find(R"(<h1 id="title" style="color: #7BD88F;">Take a break</h1>)") != std::string::npos);

    // Undo goes back a step at a time; redo comes forward again.
    page = d.ui({{"op", "undo"}, {"path", "UI/pause.noorui"}});
    CHECK(d.file("UI/pause.noorui").find(R"(<h1 id="title">Take a break</h1>)") != std::string::npos);
    CHECK(page["can_redo"].get<bool>());
    d.ui({{"op", "redo"}, {"path", "UI/pause.noorui"}});
    CHECK(d.file("UI/pause.noorui") == built);

    // Someone edits the file in NoorCode; the next builder edit and its undo keep that edit.
    std::string by_hand = built;
    by_hand.replace(by_hand.find("Take a break"), 12, "Have a rest!");
    REQUIRE(noor::fs::write_file_atomic(d.dir / "UI/pause.noorui", by_hand));
    d.ui({{"op", "edit"}, {"path", "UI/pause.noorui"}, {"edit", {{"op", "remove"}, {"path", {0, 4}}}}});  // Quit
    CHECK(d.file("UI/pause.noorui").find("quit") == std::string::npos);
    d.ui({{"op", "undo"}, {"path", "UI/pause.noorui"}});
    CHECK(d.file("UI/pause.noorui") == by_hand);

    // The source, and replacing it (one undo step).
    CHECK(d.ui({{"op", "source"}, {"path", "UI/pause.noorui"}})["text"] == by_hand);
    d.ui({{"op", "set_source"}, {"path", "UI/pause.noorui"}, {"text", "<rml><body><p>Hi</p></body></rml>"}});
    CHECK(d.ui({{"op", "tree"}, {"path", "UI/pause.noorui"}})["tree"]["children"][0]["text"] == "Hi");
    d.ui({{"op", "undo"}, {"path", "UI/pause.noorui"}});
    CHECK(d.file("UI/pause.noorui") == by_hand);
}

TEST_CASE("ui api: mistakes are refused with a reason and nothing written") {
    UiDoc d;
    d.ui({{"op", "new"}, {"path", "UI/hud.noorui"}, {"template", "hud"}});
    const std::string before = d.file("UI/hud.noorui");
    CHECK(d.fails({{"op", "new"}, {"path", "../outside.noorui"}}));
    CHECK(d.fails({{"op", "new"}, {"path", "UI/page.txt"}}));
    CHECK(d.fails({{"op", "new"}, {"path", "UI/x.noorui"}, {"template", "castle"}}));
    CHECK(d.fails({{"op", "tree"}, {"path", "UI/missing.noorui"}}));
    CHECK(d.fails({{"op", "edit"}, {"path", "UI/hud.noorui"}, {"edit", {{"op", "remove"}, {"path", {9}}}}}));
    CHECK(std::string(noor_last_error()).find("no element at that path") != std::string::npos);
    CHECK(d.fails({{"op", "edit"}, {"path", "UI/hud.noorui"}, {"edit", {{"op", "insert"}, {"path", {}}, {"markup", "<div>"}}}}));
    CHECK(d.fails({{"op", "undo"}, {"path", "UI/hud.noorui"}}));  // nothing to undo
    CHECK(d.fails({{"op", "explode"}, {"path", "UI/hud.noorui"}}));
    CHECK(d.file("UI/hud.noorui") == before);
    // A page that doesn't read still answers, with the reason (its text can be fixed in NoorCode).
    d.ui({{"op", "set_source"}, {"path", "UI/hud.noorui"}, {"text", "<rml><body><div></body></rml>"}});
    const Json broken = d.ui({{"op", "tree"}, {"path", "UI/hud.noorui"}});
    CHECK_FALSE(broken.contains("tree"));
    CHECK(broken["error"].get<std::string>().find("closes <div>") != std::string::npos);
}
