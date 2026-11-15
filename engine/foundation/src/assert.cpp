#include "noor/foundation/assert.h"

#include "noor/foundation/log.h"

#include <atomic>
#include <cstdlib>

#if defined(_WIN32)
#include <intrin.h>
extern "C" __declspec(dllimport) int __stdcall IsDebuggerPresent();
#endif

namespace noor {
namespace {

void default_handler(std::string_view expression, std::string_view message,
                     const std::source_location& where) {
    log::write_record(LogLevel::Fatal, "Assert",
                      std::format("Assertion failed: {} ({})", expression, message), where);
    log::flush();
#if defined(_WIN32)
    if (IsDebuggerPresent()) {
        __debugbreak();
    }
#endif
    std::abort();
}

std::atomic<AssertHandler> g_handler{&default_handler};

} // namespace

AssertHandler set_assert_handler(AssertHandler handler) noexcept {
    return g_handler.exchange(handler ? handler : &default_handler);
}

void assert_failed(std::string_view expression, std::string_view message,
                   const std::source_location& where) {
    g_handler.load()(expression, message, where);
}

} // namespace noor
