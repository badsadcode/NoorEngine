#pragma once
// Recoverable errors. Functions that can fail return noor::Result<T>.
// Asserts (assert.h) are reserved for broken internal invariants.

#include <cstdint>
#include <expected>
#include <source_location>
#include <string>
#include <string_view>

namespace noor {

enum class ErrorCode : std::uint16_t {
    Unknown = 0,
    InvalidArgument,
    NotFound,
    AlreadyExists,
    IoError,
    ParseError,
    Unsupported,
    OutOfMemory,
    VersionMismatch,
    PlatformError,
    GraphicsError,
};

[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;

struct Error {
    ErrorCode code = ErrorCode::Unknown;
    std::string message;
    std::source_location where = std::source_location::current();

    // "GraphicsError: vkCreateDevice failed (VK_ERROR_INITIALIZATION_FAILED) at device.cpp:88"
    [[nodiscard]] std::string describe() const;
};

template <class T>
using Result = std::expected<T, Error>;

using Status = std::expected<void, Error>;

// Helper: `return noor::fail(ErrorCode::NotFound, "no such asset {}", id);`
[[nodiscard]] inline std::unexpected<Error>
fail(ErrorCode code, std::string message, std::source_location where = std::source_location::current()) {
    return std::unexpected<Error>(Error{code, std::move(message), where});
}

} // namespace noor
