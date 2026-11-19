// Sprite sheets (Milestone 8B, part 5, ADR-035): slicing exactly on the grid, animations frame by frame, the file.
#include "noor/asset/sprite_asset.h"

#include <ostream>
#include <doctest/doctest.h>

using namespace noor;
using asset::Json;

TEST_CASE("sprites: a sheet sliced by columns and rows gives frames exactly on the grid") {
    auto sheet = asset::parse_sprite_sheet(Json{{"noor_sprite", 1}, {"grid", {{"columns", 8}, {"rows", 4}}}});
    REQUIRE(sheet.has_value());
    REQUIRE(asset::slice(*sheet, 512, 256).has_value());
    REQUIRE(sheet->frames.size() == 32);
    CHECK(sheet->frame_width == 64);
    CHECK(sheet->frame_height == 64);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 8; ++c) {
            const auto& f = sheet->frames[static_cast<std::size_t>(r * 8 + c)];  // left to right, top to bottom
            CHECK(f.x == c * 64);
            CHECK(f.y == r * 64);
            CHECK(f.width == 64);
            CHECK(f.height == 64);
            CHECK(f.uv_offset.x == static_cast<float>(c * 64) / 512.0f);
            CHECK(f.uv_offset.y == static_cast<float>(r * 64) / 256.0f);
            CHECK(f.uv_scale.x == 64.0f / 512.0f);
            CHECK(f.uv_scale.y == 64.0f / 256.0f);
        }
}

TEST_CASE("sprites: margins and spacing, and slicing by frame size") {
    // A 2-pixel border round the sheet and 2 pixels between 30x40 frames: 4 x 2 of them in 130 x 86 (2 + 4*30 + 3*2 + 2).
    auto sheet = asset::parse_sprite_sheet(Json{{"noor_sprite", 1}, {"grid", {{"frame_width", 30}, {"frame_height", 40}, {"margin", 2}, {"spacing", 2}}}});
    REQUIRE(sheet.has_value());
    REQUIRE(asset::slice(*sheet, 130, 86).has_value());
    CHECK(sheet->columns == 4);
    CHECK(sheet->rows == 2);
    CHECK(sheet->frames[0].x == 2);
    CHECK(sheet->frames[0].y == 2);
    CHECK(sheet->frames[1].x == 34);   // 2 + 30 + 2
    CHECK(sheet->frames[4].y == 44);   // 2 + 40 + 2
    CHECK(sheet->frames[7].x + sheet->frames[7].width == 128);  // the last column ends at the margin
    CHECK(sheet->frames[7].y + sheet->frames[7].height == 84);  // and the last row
    // A grid that doesn't fit is refused, not guessed.
    auto bad = asset::parse_sprite_sheet(Json{{"noor_sprite", 1}, {"grid", {{"frame_width", 300}, {"frame_height", 40}}}});
    REQUIRE(bad.has_value());
    CHECK(!asset::slice(*bad, 130, 86).has_value());
}

TEST_CASE("sprites: animations show the right frame at the right time, looping or holding") {
    auto sheet = asset::parse_sprite_sheet(Json::parse(R"({"noor_sprite": 1, "animations": {
        "walk": {"frames": {"from": 8, "to": 11}, "fps": 8, "loop": true},
        "die": {"frames": [28, 29, 30, 31], "fps": 4, "loop": false},
        "back": {"frames": {"from": 3, "to": 1}}}})"));
    REQUIRE(sheet.has_value());
    CHECK(sheet->frame_at("walk", 0.0) == 8);
    CHECK(sheet->frame_at("walk", 0.124) == 8);
    CHECK(sheet->frame_at("walk", 0.125) == 9);  // frame k from k / fps
    CHECK(sheet->frame_at("walk", 0.49) == 11);
    CHECK(sheet->frame_at("walk", 0.5) == 8);    // and round again
    CHECK(sheet->frame_at("die", 0.3) == 29);
    CHECK(sheet->frame_at("die", 10.0) == 31);   // holds the last frame
    CHECK(sheet->frame_at("nothing", 3.0) == 0);
    REQUIRE(sheet->animations.at("back").frames.size() == 3);
    CHECK(sheet->animations.at("back").frames[0] == 3);  // from 3 down to 1
    // A typo in a range doesn't make billions of frames.
    auto typo = asset::parse_sprite_sheet(Json::parse(R"({"noor_sprite": 1, "animations": {"oops": {"frames": {"from": 0, "to": 2000000000}}}})"));
    REQUIRE(typo.has_value());
    CHECK(typo->animations.at("oops").frames.size() == 65536);
}

TEST_CASE("sprites: defaults, the size in metres, and the file round trip") {
    auto sheet = asset::parse_sprite_sheet(Json{{"noor_sprite", 1}, {"grid", {{"columns", 4}, {"rows", 1}}}, {"pixels_per_metre", 32}});
    REQUIRE(sheet.has_value());
    CHECK(sheet->nearest);  // pixel art unless told "linear"
    CHECK(sheet->pivot.x == 0.5f);
    CHECK(sheet->pivot.y == 0.0f);  // feet at the bottom centre
    REQUIRE(asset::slice(*sheet, 256, 64).has_value());
    CHECK(sheet->frame_size_m().x == doctest::Approx(2.0f));  // 64 px at 32 px a metre
    CHECK(sheet->frame_size_m().y == doctest::Approx(2.0f));
    sheet->animations["idle"] = {{0, 1}, 2.0f, true};
    auto back = asset::parse_sprite_sheet(asset::sprite_file_json(*sheet));
    REQUIRE(back.has_value());
    CHECK(back->columns == 4);
    CHECK(back->pixels_per_metre == 32.0f);
    CHECK(back->animations.at("idle").frames.size() == 2);
    CHECK(!asset::parse_sprite_sheet(Json{{"not", "a sheet"}}).has_value());
}
