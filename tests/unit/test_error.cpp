#include "noor/foundation/error.h"

#include <ostream>  // doctest stringifies std::string_view via operator<<
#include <doctest/doctest.h>

namespace {

noor::Result<int> parse_positive(int value) {
    if (value <= 0) {
        return noor::fail(noor::ErrorCode::InvalidArgument, "value must be positive");
    }
    return value;
}

} // namespace

TEST_CASE("error: Result carries a value on success") {
    const auto r = parse_positive(5);
    REQUIRE(r.has_value());
    CHECK(*r == 5);
}

TEST_CASE("error: Result carries code, message and location on failure") {
    const auto r = parse_positive(-1);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == noor::ErrorCode::InvalidArgument);
    CHECK(r.error().message == "value must be positive");
    CHECK(std::string_view(r.error().where.file_name()).ends_with("test_error.cpp"));
}

TEST_CASE("error: describe() is human readable") {
    const auto r = parse_positive(0);
    const std::string text = r.error().describe();
    CHECK(text.starts_with("InvalidArgument: value must be positive at test_error.cpp:"));
}

TEST_CASE("error: every code has a name") {
    for (int i = 0; i <= static_cast<int>(noor::ErrorCode::DeviceLost); ++i) {
        const auto name = noor::to_string(static_cast<noor::ErrorCode>(i));
        CHECK_FALSE(name.empty());
        if (i != 0) {
            CHECK(name != "Unknown");
        }
    }
}
