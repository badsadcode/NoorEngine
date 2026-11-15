#include "noor/asset/migration.h"

#include <format>

namespace noor::asset {

Migrations& Migrations::add(int from, std::string description, Step step) {
    steps_.push_back({from, std::move(description), std::move(step)});
    return *this;
}

Result<std::vector<std::string>> Migrations::upgrade(Json& document, std::string_view source) const {
    if (!document.is_object() || !document.contains(key_) || !document[key_].is_number_integer()) {
        return fail(ErrorCode::ParseError, std::format("{}: missing \"{}\" version", source, key_));
    }
    int version = document[key_].get<int>();
    if (version <= 0) return fail(ErrorCode::ParseError, std::format("{}: invalid version {}", source, version));
    if (version > current_) {
        return fail(ErrorCode::VersionMismatch,
                    std::format("{}: format {} needs a newer NoorEngine (this one reads up to {})", source, version, current_));
    }
    std::vector<std::string> applied;
    while (version < current_) {
        const Entry* entry = nullptr;
        for (const auto& e : steps_) {
            if (e.from == version) entry = &e;
        }
        if (!entry) {
            return fail(ErrorCode::VersionMismatch, std::format("{}: no migration from format {}", source, version));
        }
        if (auto s = entry->step(document); !s) {
            return fail(s.error().code, std::format("{}: migration {} -> {} failed: {}", source, version, version + 1,
                                                    s.error().message));
        }
        applied.push_back(std::format("{} -> {}: {}", version, version + 1, entry->description));
        ++version;
        document[key_] = version;
    }
    return applied;
}

} // namespace noor::asset
