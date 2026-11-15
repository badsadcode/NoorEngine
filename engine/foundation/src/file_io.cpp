#include "noor/foundation/file_io.h"

#include <format>
#include <fstream>
#include <system_error>

namespace noor::fs {

Result<std::string> read_file(const std::filesystem::path& path, std::uint64_t max_bytes) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return fail(std::filesystem::exists(path) ? ErrorCode::IoError : ErrorCode::NotFound,
                    std::format("Cannot read '{}': {}", path.generic_string(), ec.message()));
    }
    if (size > max_bytes) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("'{}' is {} bytes, above the {} byte limit", path.generic_string(), size, max_bytes));
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return fail(ErrorCode::IoError, std::format("Cannot open '{}'", path.generic_string()));
    }
    std::string data(static_cast<std::size_t>(size), '\0');
    in.read(data.data(), static_cast<std::streamsize>(size));
    if (!in) {
        return fail(ErrorCode::IoError, std::format("Short read on '{}'", path.generic_string()));
    }
    return data;
}

Status write_file_atomic(const std::filesystem::path& path, std::string_view data, bool keep_backup) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return fail(ErrorCode::IoError, std::format("Cannot create folder '{}': {}",
                                                        path.parent_path().generic_string(), ec.message()));
        }
    }

    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return fail(ErrorCode::IoError, std::format("Cannot write '{}'", tmp.generic_string()));
        }
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out) {
            std::filesystem::remove(tmp, ec);
            return fail(ErrorCode::IoError, std::format("Write failed for '{}' (disk full?)", tmp.generic_string()));
        }
    }

    if (keep_backup && std::filesystem::exists(path)) {
        std::filesystem::path bak = path;
        bak += ".bak";
        std::filesystem::copy_file(path, bak, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            return fail(ErrorCode::IoError, std::format("Cannot keep backup '{}': {}", bak.generic_string(), ec.message()));
        }
    }

    std::filesystem::rename(tmp, path, ec);  // replaces the target atomically on NTFS and POSIX
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return fail(ErrorCode::IoError, std::format("Cannot replace '{}': {}", path.generic_string(), ec.message()));
    }
    return {};
}

} // namespace noor::fs
