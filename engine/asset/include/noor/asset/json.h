#pragma once
// JSON helpers shared by projects, metas and scenes (ADR-005: deterministic text).

#include "noor/foundation/error.h"
#include "noor/foundation/guid.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace noor::asset {

using Json = nlohmann::ordered_json;

// Deterministic text: 2-space indent, '\n' line endings, trailing newline, UTF-8 kept as-is.
[[nodiscard]] std::string to_text(const Json& json);

// Parses text; on failure the error names `source` (usually the file) and the position.
[[nodiscard]] Result<Json> parse_json(std::string_view text, std::string_view source);

// Reads a GUID string field; nothing if missing or malformed.
[[nodiscard]] std::optional<Guid> guid_field(const Json& object, std::string_view key);

} // namespace noor::asset
