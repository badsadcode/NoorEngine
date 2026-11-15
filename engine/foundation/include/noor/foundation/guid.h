#pragma once
// 128-bit globally unique identifiers (RFC 9562 version 4, random).
// Used for persistent identity of assets, scenes, entities and plugins.
// Text form: lowercase "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx".

#include "noor/foundation/error.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace noor {

struct Guid {
    std::array<std::uint8_t, 16> bytes{};

    // New random v4 GUID from the OS cryptographic random source.
    [[nodiscard]] static Guid generate();
    [[nodiscard]] static Result<Guid> parse(std::string_view text);

    [[nodiscard]] bool is_nil() const noexcept;
    [[nodiscard]] std::string to_string() const;

    friend auto operator<=>(const Guid&, const Guid&) = default;
};

} // namespace noor

template <>
struct std::hash<noor::Guid> {
    std::size_t operator()(const noor::Guid& guid) const noexcept;
};
