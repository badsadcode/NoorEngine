#include "noor/fx/fx_asset.h"

#include "noor/foundation/file_io.h"

#include <format>

namespace noor::fx {

using asset::Json;

Json fx_file_json(std::string_view name, const Json& emitter, std::string_view preset) {
    Json j{{"noor_fx", kFxFormat}, {"name", std::string(name)}, {"emitter", emitter.is_object() ? emitter : Json::object()}};
    if (!preset.empty()) j["preset"] = std::string(preset);
    return j;
}

Result<Json> load_fx_file(const std::filesystem::path& file) {
    auto text = fs::read_file(file, 4 << 20);
    if (!text) return std::unexpected(text.error());
    auto json = asset::parse_json(*text, file.generic_string());
    if (!json) return std::unexpected(json.error());
    if (!json->is_object() || !json->contains("noor_fx") || !(*json)["noor_fx"].is_number_integer())
        return fail(ErrorCode::ParseError, std::format("{}: not an effect file (no \"noor_fx\")", file.filename().string()));
    if ((*json)["noor_fx"].get<int>() > kFxFormat)
        return fail(ErrorCode::ParseError, std::format("{}: made by a newer NoorEngine (format {})", file.filename().string(), (*json)["noor_fx"].get<int>()));
    if (!json->contains("emitter") || !(*json)["emitter"].is_object())
        return fail(ErrorCode::ParseError, std::format("{}: no \"emitter\" settings", file.filename().string()));
    return std::move(*json);
}

Json merge_emitter(const Json& asset_emitter, const Json& component) {
    Json merged = asset_emitter.is_object() ? asset_emitter : Json::object();
    if (component.is_object())
        for (const auto& [key, value] : component.items())
            if (key != "asset") merged[key] = value;
    return merged;
}

} // namespace noor::fx
