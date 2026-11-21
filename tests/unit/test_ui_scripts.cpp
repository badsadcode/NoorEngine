// Game UI, part 6b (ADR-041), without a GPU: elements by id (text escaped, markup, classes, attributes, styles, control
// values), events (clicks naming the element with an id, changes with their value), the keyboard (Tab moves focus,
// Enter clicks), typed text into a text box, and documents in a running World: the ui_document component, scripts'
// requests (runtime::api::ui), events delivered as messages from the document's emitter the frame after, and documents
// closed with the entity they belong to.
#include "noor/foundation/guid.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"
#include "noor/ui/ui.h"
#include "noor/ui/world_ui.h"

#include <ostream>
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <set>

using namespace noor;
namespace stdfs = std::filesystem;

namespace {

struct Folder {
    stdfs::path root = stdfs::temp_directory_path() / ("noor_ui6b_" + Guid::generate().to_string());
    Folder() { stdfs::create_directories(root / "UI"); }
    ~Folder() {
        std::error_code ec;
        stdfs::remove_all(root, ec);
    }
    void write(const std::string& path, const std::string& text) const { std::ofstream(root / path, std::ios::binary) << text; }
};

struct FakeTextures {
    std::set<std::uint32_t> alive;
    std::uint32_t next = 100;
    ui::TextureHost host() {
        ui::TextureHost h;
        h.create = [this](std::uint32_t, std::uint32_t, std::span<const std::uint8_t>) -> Result<render::TextureHandle> {
            alive.insert(next);
            return render::TextureHandle{next++};
        };
        h.destroy = [this](render::TextureHandle t) { alive.erase(t.id); };
        return h;
    }
};

constexpr const char* kForm = R"(<rml><head><title>Form</title>
<style>
  .button { display: block; tab-index: auto; focus: auto; padding: 4dp; }
  input { display: block; width: 200dp; height: 24dp; }
</style></head>
<body>
  <div id="go" class="button"><span id="label">Go</span></div>
  <div id="stop" class="button">Stop</div>
  <input id="name" type="text" value=""/>
  <input id="volume" type="range" min="0" max="1" step="0.05" value="0.5"/>
  <input id="subtitles" type="checkbox"/>
  <p id="note">old</p>
</body></rml>)";

struct Opened {
    Folder folder;
    FakeTextures textures;
    std::unique_ptr<ui::Ui> ui;
    ui::DocumentId doc;
    Opened() {
        folder.write("UI/form.noorui", kForm);
        auto made = ui::Ui::create({folder.root, textures.host(), {}});
        REQUIRE(made);
        ui = std::move(*made);
        ui->set_viewport(0, 0, 1280, 720);
        auto d = ui->open("UI/form.noorui");
        REQUIRE(d);
        doc = *d;
        ui->update();
    }
};

// Records what the World delivers to it, as a script system would.
class Recorder final : public runtime::System {
public:
    std::vector<std::string> heard;
    std::string_view name() const override { return "recorder"; }
    void deliver(runtime::World&, std::uint64_t, const std::optional<Guid>&, std::string_view message, const asset::Json& data) override {
        heard.push_back(std::string(message) + " " + data.value("element", std::string()));
    }
};

} // namespace

TEST_CASE("UI: elements by id, and what happens on them") {
    Opened o;
    auto& u = *o.ui;
    CHECK(u.find("UI/form.noorui") == o.doc);
    CHECK(u.find("/UI/form.noorui") == o.doc);
    CHECK_FALSE(u.find("UI/other.noorui"));

    // Text is shown as it is: markup in it is escaped.
    CHECK(u.set_text(o.doc, "note", "<b>5 & 6</b>"));
    CHECK(u.value(o.doc, "#note") == asset::Json("&lt;b&gt;5 &amp; 6&lt;/b&gt;"));
    CHECK(u.set_rml(o.doc, "note", "<b>bold</b>"));
    CHECK(u.value(o.doc, "note") == asset::Json("<b>bold</b>"));
    CHECK(u.set_class(o.doc, "go", "big", true));
    CHECK(u.set_attribute(o.doc, "stop", "disabled", true));
    CHECK(u.set_style(o.doc, "go", "width", "100dp"));
    CHECK_FALSE(u.set_text(o.doc, "missing", "x"));
    CHECK_FALSE(u.value(o.doc, "missing"));
    CHECK_FALSE(u.click(o.doc, "missing"));

    // Controls: a range is a number, a checkbox true / false, a text box a string.
    CHECK(u.set_value(o.doc, "volume", 0.25));
    CHECK(u.value(o.doc, "volume")->get<double>() == doctest::Approx(0.25));
    CHECK(u.value(o.doc, "subtitles") == asset::Json(false));
    CHECK(u.set_value(o.doc, "subtitles", true));
    CHECK(u.value(o.doc, "subtitles") == asset::Json(true));
    CHECK(u.set_value(o.doc, "name", "Coth"));
    CHECK(u.value(o.doc, "name") == asset::Json("Coth"));
    (void)u.take_events();

    // A click names the element it happened on (the span has an id of its own).
    CHECK(u.click(o.doc, "go"));
    CHECK(u.click(o.doc, "label"));
    std::vector<std::string> clicks;
    for (const auto& e : u.take_events())
        if (e.type == "click") {
            CHECK(e.document == o.doc);
            clicks.push_back(e.element);
        }
    CHECK(clicks == std::vector<std::string>{"go", "label"});
    CHECK(u.take_events().empty());

    // A changed value says what it is now.
    CHECK(u.set_value(o.doc, "volume", 0.75));
    bool changed = false;
    for (const auto& e : u.take_events())
        if (e.type == "change" && e.element == "volume") {
            changed = true;
            CHECK(e.value.get<double>() == doctest::Approx(0.75));
        }
    CHECK(changed);
}

TEST_CASE("UI: the keyboard moves focus and clicks, and text boxes take typing") {
    Opened o;
    auto& u = *o.ui;
    // Tab: to the first focusable thing; Enter clicks what has focus; Tab again: the next.
    u.key(ui::Key::Tab, true);
    u.key(ui::Key::Tab, false);
    u.update();
    u.key(ui::Key::Enter, true);
    u.key(ui::Key::Enter, false);
    u.key(ui::Key::Tab, true);
    u.key(ui::Key::Tab, false);
    std::vector<std::string> seen;
    for (const auto& e : u.take_events())
        if (e.type == "focus" || e.type == "click") seen.push_back(e.type + " " + e.element);
    CHECK(seen == std::vector<std::string>{"focus go", "click go", "focus stop"});

    // A text box with focus wants typed text; typing and Backspace edit it.
    CHECK_FALSE(u.wants_text());
    CHECK(u.focus(o.doc, "name"));
    u.update();
    CHECK(u.wants_text());
    u.text("C\xC3\xB6th!");  // "Cöth!"
    u.key(ui::Key::Backspace, true);
    u.key(ui::Key::Backspace, false);
    CHECK(u.value(o.doc, "name") == asset::Json("C\xC3\xB6th"));
    CHECK(u.focus(o.doc, "go"));
    u.update();
    CHECK_FALSE(u.wants_text());
}

TEST_CASE("UI: documents in a running World, events as messages, closed with their entity") {
    Folder f;
    f.write("UI/form.noorui", kForm);
    f.write("UI/hud.noorui", "<rml><head></head><body><div id=\"score\">0</div></body></rml>");
    FakeTextures tex;
    scene::Scene s;
    s.id = Guid::generate();
    scene::Entity hud;
    hud.id = Guid::generate();
    hud.name = "HUD";
    hud.components["ui_document"] = {{"document", "UI/hud.noorui"}};
    s.entities.push_back(hud);
    scene::Entity owner;
    owner.id = Guid::generate();
    owner.name = "Menu";
    s.entities.push_back(owner);
    runtime::World world(std::move(s));
    auto recorder = std::make_unique<Recorder>();
    Recorder* rec = recorder.get();
    world.add_system(std::move(recorder));

    // Without a UI: a reason, not a crash.
    CHECK(std::holds_alternative<std::string>(runtime::api::ui(world, owner.id, {{"op", "open"}, {"path", "UI/form.noorui"}})));

    ui::WorldUi wui({f.root, tex.host(), [&] { return world.real_time(); }});
    CHECK(wui.ui() == nullptr);  // nothing open yet: RmlUi hasn't started
    wui.attach(world);
    REQUIRE(wui.ui() != nullptr);  // the HUD component's document
    const auto hud_doc = wui.find("UI/hud.noorui");
    REQUIRE(hud_doc);

    auto opened = runtime::api::ui(world, owner.id, {{"op", "open"}, {"path", "UI/form.noorui"}});
    REQUIRE(std::holds_alternative<asset::Json>(opened));
    const asset::Json doc = std::get<asset::Json>(opened);
    const auto d = doc["document"].get<std::uint32_t>();
    CHECK(doc["emitter"] == world.ui_emitter(d).to_string());
    CHECK(world.ui_emitter(d) != world.ui_emitter(*hud_doc));
    auto found = runtime::api::ui(world, owner.id, {{"op", "find"}, {"path", "UI/hud.noorui"}});
    REQUIRE(std::holds_alternative<asset::Json>(found));
    CHECK(std::get<asset::Json>(found)["document"] == *hud_doc);

    // Mistakes name themselves.
    auto bad = runtime::api::ui(world, owner.id, {{"op", "set_text"}, {"document", d}, {"element", "nope"}, {"text", "x"}});
    REQUIRE(std::holds_alternative<std::string>(bad));
    CHECK(std::get<std::string>(bad).find("nope") != std::string::npos);
    CHECK(std::holds_alternative<std::string>(runtime::api::ui(world, owner.id, {{"op", "shout"}, {"document", d}, {"element", "go"}})));

    // Events become messages from the document's emitter, "click" and "click#go", delivered the frame after.
    world.listen(world.ui_emitter(d), "click#go", owner.id, rec, 1);
    world.listen(world.ui_emitter(d), "click", owner.id, rec, 2);
    CHECK(std::holds_alternative<asset::Json>(runtime::api::ui(world, owner.id, {{"op", "click"}, {"document", d}, {"element", "#go"}})));
    (void)wui.draw(0, 0, 1280, 720);  // after a frame: events forwarded and queued
    CHECK(rec->heard.empty());
    world.frame(1.0 / 60.0, {});
    CHECK(rec->heard == std::vector<std::string>{"click go", "click#go go"});

    // The owner goes: its document closes; the component's stays.
    world.destroy(owner.id);
    world.frame(1.0 / 60.0, {});
    CHECK_FALSE(wui.is_open(d));
    CHECK(wui.is_open(*hud_doc));
}

TEST_CASE("UI: text boxes copy and paste through the host's clipboard") {
    Folder f;
    f.write("UI/form.noorui", kForm);
    FakeTextures tex;
    std::string clipboard = "pasted";
    ui::UiDesc desc{f.root, tex.host(), {}};
    desc.set_clipboard = [&](const std::string& t) { clipboard = t; };
    desc.get_clipboard = [&] { return clipboard; };
    auto made = ui::Ui::create(std::move(desc));
    REQUIRE(made);
    auto& u = **made;
    u.set_viewport(0, 0, 1280, 720);
    const auto d = u.open("UI/form.noorui");
    REQUIRE(d);
    REQUIRE(u.set_value(*d, "name", "Wispr"));
    REQUIRE(u.focus(*d, "name"));
    u.update();
    const ui::Modifiers ctrl{.ctrl = true};
    u.key(ui::Key::A, true, ctrl);  // select all, copy: the host's clipboard has it
    u.key(ui::Key::A, false, ctrl);
    u.key(ui::Key::C, true, ctrl);
    u.key(ui::Key::C, false, ctrl);
    CHECK(clipboard == "Wispr");
    clipboard = "Uncle ";
    u.key(ui::Key::Home, true);  // paste at the start
    u.key(ui::Key::Home, false);
    u.key(ui::Key::V, true, ctrl);
    u.key(ui::Key::V, false, ctrl);
    CHECK(u.value(*d, "name") == asset::Json("Uncle Wispr"));
}
