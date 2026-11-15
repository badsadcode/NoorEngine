#pragma once
// Assertions for broken internal invariants (programmer errors), never for bad content
// or user input; those return noor::Result instead.
//
// NOOR_ASSERT is active in all build configurations for now: an engine that silently
// continues with corrupted state is worse than one that stops. Revisit with profiling data.

#include <source_location>
#include <string_view>

namespace noor {

// Called when an assertion fails. The default handler logs a Fatal record, flushes logs,
// breaks into an attached debugger and aborts. Tests can install their own handler.
using AssertHandler = void (*)(std::string_view expression, std::string_view message,
                               const std::source_location& where);

AssertHandler set_assert_handler(AssertHandler handler) noexcept;

void assert_failed(std::string_view expression, std::string_view message,
                   const std::source_location& where = std::source_location::current());

} // namespace noor

#define NOOR_ASSERT(condition, message)                          \
    do {                                                         \
        if (!(condition)) [[unlikely]] {                         \
            ::noor::assert_failed(#condition, (message));        \
        }                                                        \
    } while (false)
