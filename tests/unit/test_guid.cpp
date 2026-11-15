#include "noor/foundation/guid.h"

#include <ostream>  // doctest stringifies std::string via operator<<
#include <doctest/doctest.h>
#include <unordered_set>

TEST_CASE("guid: default-constructed GUID is nil") {
    CHECK(noor::Guid{}.is_nil());
    CHECK(noor::Guid{}.to_string() == "00000000-0000-0000-0000-000000000000");
}

TEST_CASE("guid: generated GUIDs are version 4, RFC variant, and not nil") {
    for (int i = 0; i < 100; ++i) {
        const auto g = noor::Guid::generate();
        CHECK_FALSE(g.is_nil());
        CHECK((g.bytes[6] >> 4) == 4);
        CHECK((g.bytes[8] & 0xC0) == 0x80);
        const std::string s = g.to_string();
        CHECK(s[14] == '4');
    }
}

TEST_CASE("guid: 10,000 generated GUIDs are unique") {
    std::unordered_set<noor::Guid> seen;
    for (int i = 0; i < 10'000; ++i) {
        CHECK(seen.insert(noor::Guid::generate()).second);
    }
}

TEST_CASE("guid: to_string / parse round-trip") {
    const auto g = noor::Guid::generate();
    const auto parsed = noor::Guid::parse(g.to_string());
    REQUIRE(parsed.has_value());
    CHECK(*parsed == g);
}

TEST_CASE("guid: parse accepts uppercase and produces lowercase text") {
    const auto parsed = noor::Guid::parse("6B3FD900-ABCD-4EF0-8123-456789ABCDEF");
    REQUIRE(parsed.has_value());
    CHECK(parsed->to_string() == "6b3fd900-abcd-4ef0-8123-456789abcdef");
}

TEST_CASE("guid: parse rejects malformed input with ParseError") {
    const char* bad[] = {
        "",
        "not-a-guid",
        "6b3fd900abcd4ef08123456789abcdef",       // no dashes
        "6b3fd900-abcd-4ef0-8123-456789abcdeg",   // bad hex
        "6b3fd900-abcd-4ef0-8123_456789abcdef",   // wrong separator
        "6b3fd900-abcd-4ef0-8123-456789abcdef0",  // too long
    };
    for (const char* text : bad) {
        CAPTURE(text);
        const auto r = noor::Guid::parse(text);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().code == noor::ErrorCode::ParseError);
    }
}

TEST_CASE("guid: ordering and hashing are consistent with equality") {
    const auto a = noor::Guid::generate();
    const auto b = a;
    CHECK(a == b);
    CHECK_FALSE(a < b);
    CHECK(std::hash<noor::Guid>{}(a) == std::hash<noor::Guid>{}(b));
}
