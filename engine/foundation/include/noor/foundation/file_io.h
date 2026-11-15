#pragma once
// File helpers with the safety rules of ADR-005: atomic replacement and a backup.

#include "noor/foundation/error.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace noor::fs {

// Reads a whole file. Fails with NotFound / IoError; refuses files above `max_bytes`
// (imported files are untrusted input, spec 28.2).
[[nodiscard]] Result<std::string> read_file(const std::filesystem::path& path,
                                            std::uint64_t max_bytes = 1ull << 30);

// Writes `data` so that a crash leaves either the old or the new file, never a half-written one:
// write <path>.tmp, flush, (optionally) keep the old file as <path>.bak, then replace.
[[nodiscard]] Status write_file_atomic(const std::filesystem::path& path, std::string_view data,
                                       bool keep_backup = true);

} // namespace noor::fs
