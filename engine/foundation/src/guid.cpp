#include "noor/foundation/guid.h"

#include "noor/foundation/sha256.h"

#include <cstring>
#include <format>
#include <random>

namespace noor {
namespace {

constexpr char kHex[] = "0123456789abcdef";

int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool is_dash_position(std::size_t i) noexcept { return i == 8 || i == 13 || i == 18 || i == 23; }

} // namespace

Guid Guid::generate() {
    // std::random_device is backed by the OS CSPRNG on MSVC (rand_s), libstdc++ and libc++
    // (/dev/urandom or getrandom). Each call draws fresh entropy, so no shared engine state.
    thread_local std::random_device device;
    Guid guid;
    for (std::size_t i = 0; i < guid.bytes.size(); i += 4) {
        const std::uint32_t value = device();
        std::memcpy(&guid.bytes[i], &value, sizeof(value));
    }
    guid.bytes[6] = static_cast<std::uint8_t>((guid.bytes[6] & 0x0F) | 0x40); // version 4
    guid.bytes[8] = static_cast<std::uint8_t>((guid.bytes[8] & 0x3F) | 0x80); // RFC variant
    return guid;
}

Guid Guid::derive(const Guid& a, const Guid& b) {
    Sha256 sha;
    sha.update("noor.guid.derive.v1");  // domain separation
    sha.update(std::string_view(reinterpret_cast<const char*>(a.bytes.data()), a.bytes.size()));
    sha.update(std::string_view(reinterpret_cast<const char*>(b.bytes.data()), b.bytes.size()));
    const auto digest = sha.finish();
    Guid guid;
    std::memcpy(guid.bytes.data(), digest.data(), guid.bytes.size());
    guid.bytes[6] = static_cast<std::uint8_t>((guid.bytes[6] & 0x0F) | 0x80);  // version 8
    guid.bytes[8] = static_cast<std::uint8_t>((guid.bytes[8] & 0x3F) | 0x80);  // RFC variant
    return guid;
}

Result<Guid> Guid::parse(std::string_view text) {
    if (text.size() != 36) {
        return fail(ErrorCode::ParseError, std::format("GUID must be 36 characters, got {}", text.size()));
    }
    Guid guid;
    std::size_t byte = 0;
    for (std::size_t i = 0; i < text.size();) {
        if (is_dash_position(i)) {
            if (text[i] != '-') {
                return fail(ErrorCode::ParseError, std::format("expected '-' at position {}", i));
            }
            ++i;
            continue;
        }
        const int hi = hex_value(text[i]);
        const int lo = hex_value(text[i + 1]);
        if (hi < 0 || lo < 0) {
            return fail(ErrorCode::ParseError, std::format("invalid hex digit near position {}", i));
        }
        guid.bytes[byte++] = static_cast<std::uint8_t>((hi << 4) | lo);
        i += 2;
    }
    return guid;
}

bool Guid::is_nil() const noexcept {
    for (const auto b : bytes) {
        if (b != 0) return false;
    }
    return true;
}

std::string Guid::to_string() const {
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out.push_back('-');
        }
        out.push_back(kHex[bytes[i] >> 4]);
        out.push_back(kHex[bytes[i] & 0x0F]);
    }
    return out;
}

} // namespace noor

std::size_t std::hash<noor::Guid>::operator()(const noor::Guid& guid) const noexcept {
    // GUID bytes are already uniformly random; fold the two halves.
    std::uint64_t a = 0;
    std::uint64_t b = 0;
    std::memcpy(&a, guid.bytes.data(), 8);
    std::memcpy(&b, guid.bytes.data() + 8, 8);
    return static_cast<std::size_t>(a ^ (b * 0x9E3779B97F4A7C15ull));
}
