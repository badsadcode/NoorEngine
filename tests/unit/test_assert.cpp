#include "noor/foundation/assert.h"

#include <doctest/doctest.h>
#include <string>

namespace {
// Runtime values so the compiler cannot treat the asserted conditions as constants.
int runtime_value(int v) {
    volatile int copy = v;
    return copy;
}

int g_failures = 0;
std::string g_last_expression;

void recording_handler(std::string_view expression, std::string_view, const std::source_location&) {
    ++g_failures;
    g_last_expression = std::string(expression);
}
} // namespace

TEST_CASE("assert: passing assertions do not call the handler") {
    g_failures = 0;
    const auto previous = noor::set_assert_handler(&recording_handler);
    const int two = runtime_value(2);
    NOOR_ASSERT(two == 2, "math works");
    noor::set_assert_handler(previous);
    CHECK(g_failures == 0);
}

TEST_CASE("assert: failing assertions report the expression to the handler") {
    g_failures = 0;
    const auto previous = noor::set_assert_handler(&recording_handler);
    const int frames = runtime_value(3);
    NOOR_ASSERT(frames == 2, "frames in flight must be 2");
    noor::set_assert_handler(previous);
    CHECK(g_failures == 1);
    CHECK(g_last_expression == "frames == 2");
}
