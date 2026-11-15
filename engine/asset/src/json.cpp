#include "noor/asset/json.h"

#include <format>

namespace noor::asset {

std::string to_text(const Json& json) {
    std::string text = json.dump(2, ' ', false, Json::error_handler_t::strict);
    text.push_back('\n');
    return text;
}

Result<Json> parse_json(std::string_view text, std::string_view source) {
    try {
        return Json::parse(text.begin(), text.end());
    } catch (const Json::parse_error& e) {
        return fail(ErrorCode::ParseError, std::format("{}: invalid JSON ({})", source, e.what()));
    }
}

std::optional<Guid> guid_field(const Json& object, std::string_view key) {
    if (!object.is_object()) return std::nullopt;
    const auto it = object.find(std::string(key));
    if (it == object.end() || !it->is_string()) return std::nullopt;
    auto parsed = Guid::parse(it->get<std::string>());
    if (!parsed) return std::nullopt;
    return *parsed;
}

} // namespace noor::asset
