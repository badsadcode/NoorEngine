#include "noor/foundation/error.h"

#include <format>

namespace noor {

std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::Unknown: return "Unknown";
    case ErrorCode::InvalidArgument: return "InvalidArgument";
    case ErrorCode::NotFound: return "NotFound";
    case ErrorCode::AlreadyExists: return "AlreadyExists";
    case ErrorCode::IoError: return "IoError";
    case ErrorCode::ParseError: return "ParseError";
    case ErrorCode::Unsupported: return "Unsupported";
    case ErrorCode::OutOfMemory: return "OutOfMemory";
    case ErrorCode::VersionMismatch: return "VersionMismatch";
    case ErrorCode::PlatformError: return "PlatformError";
    case ErrorCode::GraphicsError: return "GraphicsError";
    }
    return "Unknown";
}

std::string Error::describe() const {
    std::string_view file = where.file_name();
    if (const auto slash = file.find_last_of("/\\"); slash != std::string_view::npos) {
        file = file.substr(slash + 1);
    }
    return std::format("{}: {} at {}:{}", to_string(code), message, file, where.line());
}

} // namespace noor
