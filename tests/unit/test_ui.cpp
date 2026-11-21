// Game UI (gameplay foundations part 6, ADR-041), without a GPU: documents opened from a project folder become draws
// with the built-in font's textures, the default style sheet applies, clipping becomes clip rectangles in screen pixels,
// the viewport moves everything, hidden documents draw nothing, files outside the project are refused, and only one Ui
// exists at a time.
#include "noor/foundation/guid.h"
#include "noor/ui/ui.h"

#include <ostream>
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <set>

using namespace noor;
namespace stdfs = std::filesystem;

namespace {

// A project folder with documents in it, removed afterwards.
struct Folder {
    stdfs::path root = stdfs::temp_directory_path() / ("noor_ui_" + Guid::generate().to_string());
    Folder() { stdfs::create_directories(root / "UI"); }
    ~Folder() {
        std::error_code ec;
        stdfs::remove_all(root, ec);
    }
    void write(const std::string& path, const std::string& text) const {
        std::ofstream(root / path, std::ios::binary) << text;
    }
};

// Textures in memory instead of on the GPU.
struct FakeTextures {
    std::set<std::uint32_t> alive;
    std::uint32_t next = 100;
    std::uint64_t made = 0;
    ui::TextureHost host() {
        ui::TextureHost h;
        h.create = [this](std::uint32_t w, std::uint32_t hgt, std::span<const std::uint8_t> rgba) -> Result<render::TextureHandle> {
            CHECK(rgba.size() == static_cast<std::size_t>(w) * hgt * 4);
            ++made;
            alive.insert(next);
            return render::TextureHandle{next++};
        };
        h.destroy = [this](render::TextureHandle t) { alive.erase(t.id); };
        return h;
    }
};

constexpr const char* kPage = R"(<rml><head><title>Test</title>
<style>
  body { width: 100%; height: 100%; }
  #box { width: 200dp; height: 100dp; background-color: #FF0000; }
  #clip { width: 100dp; height: 40dp; overflow: hidden; }
</style></head>
<body><div id="box">Hello</div><div id="clip"><p>one</p><p>two</p><p>three</p><p>four</p></div></body></rml>)";

} // namespace

TEST_CASE("UI: a document becomes draws with the built-in font, in the viewport") {
    Folder f;
    f.write("UI/page.noorui", kPage);
    FakeTextures tex;
    double now = 0;
    auto made = ui::Ui::create({f.root, tex.host(), [&] { return now; }});
    REQUIRE(made);
    auto& u = **made;
    u.set_viewport(100, 50, 1280, 720);
    auto doc = u.open("UI/page.noorui");
    REQUIRE(doc);
    CHECK(u.visible(*doc));
    u.update();
    const auto frame = u.draw();
    REQUIRE(!frame.draws.empty());
    CHECK(!frame.vertices.empty());
    CHECK(!frame.indices.empty());
    CHECK(tex.made > 0);  // the font's glyphs
    bool textured = false, untextured = false, clipped = false;
    for (const auto& d : frame.draws) {
        CHECK(d.first_index + d.index_count <= frame.indices.size());
        (d.texture.valid() ? textured : untextured) = true;
        // Everything is moved by the viewport's corner.
        CHECK(d.translation.x >= 100.0f);
        CHECK(d.translation.y >= 50.0f);
        if (d.clip) {
            clipped = true;
            CHECK(d.clip_rect[0] >= 100);
            CHECK(d.clip_rect[1] >= 50);
            CHECK(d.clip_rect[2] == 100);  // 100dp at 720 high: 100 pixels
            CHECK(d.clip_rect[3] == 40);
        }
    }
    CHECK(textured);    // text
    CHECK(untextured);  // the red box
    CHECK(clipped);     // the list in its box
    const auto stats = u.stats();
    CHECK(stats.documents == 1);
    CHECK(stats.draws == frame.draws.size());
    CHECK(stats.textures == tex.alive.size());

    // dp follow the picture's height: half as tall, half the size.
    u.set_viewport(0, 0, 640, 360);
    u.update();
    for (const auto& d : u.draw().draws)
        if (d.clip) CHECK(d.clip_rect[3] == 20);

    // Hidden: nothing to draw; shown again: back.
    u.set_visible(*doc, false);
    CHECK_FALSE(u.visible(*doc));
    u.update();
    CHECK(u.draw().draws.empty());
    u.set_visible(*doc, true);
    u.update();
    CHECK(!u.draw().draws.empty());

    u.close(*doc);
    u.update();
    CHECK(u.draw().draws.empty());
    CHECK(u.stats().documents == 0);
}

TEST_CASE("UI: the default style sheet gives blocks and the built-in font, and the page's own styles win") {
    Folder f;
    // Two divs with no styles of their own: blocks (one under the other), so their text isn't on one line.
    f.write("UI/plain.noorui", "<rml><head></head><body><div>first</div><div>second</div></body></rml>");
    FakeTextures tex;
    auto made = ui::Ui::create({f.root, tex.host(), {}});
    REQUIRE(made);
    auto& u = **made;
    u.set_viewport(0, 0, 1280, 720);
    REQUIRE(u.open("UI/plain.noorui"));
    u.update();
    const auto frame = u.draw();
    REQUIRE(frame.draws.size() >= 2);
    // Each line of text is one draw; the second starts lower down.
    float first_y = frame.draws[0].translation.y, last_y = frame.draws.back().translation.y;
    CHECK(last_y > first_y);
}

TEST_CASE("UI: documents can't reach outside the project, and missing ones say so") {
    Folder f;
    f.write("UI/page.noorui", kPage);
    // A page pointing at an image outside the project folder: refused (logged), the page still opens.
    f.write("UI/sneaky.noorui", "<rml><head></head><body><img src=\"../../../Windows/win.ini\"/></body></rml>");
    FakeTextures tex;
    auto made = ui::Ui::create({f.root, tex.host(), {}});
    REQUIRE(made);
    auto& u = **made;
    CHECK_FALSE(u.open("../outside.noorui"));
    CHECK_FALSE(u.open("C:/Windows/win.ini"));
    const auto missing = u.open("UI/missing.noorui");
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == ErrorCode::NotFound);
    CHECK(u.open("UI/sneaky.noorui"));
    CHECK(u.open("/UI/page.noorui"));  // from the root, like links in pages
    CHECK_FALSE(u.load_font("UI/missing.ttf"));
}

TEST_CASE("UI: one at a time, and its textures are given back when it goes") {
    Folder f;
    f.write("UI/page.noorui", kPage);
    FakeTextures tex;
    {
        auto first = ui::Ui::create({f.root, tex.host(), {}});
        REQUIRE(first);
        CHECK_FALSE(ui::Ui::create({f.root, tex.host(), {}}));
        REQUIRE((*first)->open("UI/page.noorui"));
        (*first)->update();
        (void)(*first)->draw();
        CHECK(!tex.alive.empty());
    }
    CHECK(tex.alive.empty());
    // Gone: another can start (RmlUi starts again).
    auto again = ui::Ui::create({f.root, tex.host(), {}});
    REQUIRE(again);
    REQUIRE((*again)->open("UI/page.noorui"));
    (*again)->update();
    CHECK(!(*again)->draw().draws.empty());
}
