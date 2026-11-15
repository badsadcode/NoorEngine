#pragma once
// SHA-256 (FIPS 180-4). Used to recognise files by content (asset moves, manifest checksums),
// not for security decisions.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace noor {

class Sha256 {
public:
    Sha256() noexcept;
    void update(std::string_view data) noexcept;
    [[nodiscard]] std::array<std::uint8_t, 32> finish() noexcept;

    // Lowercase hex digest of `data`.
    [[nodiscard]] static std::string hex(std::string_view data);

private:
    void block(const std::uint8_t* p) noexcept;
    std::array<std::uint32_t, 8> h_;
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t total_ = 0;
};

} // namespace noor
