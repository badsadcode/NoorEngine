#pragma once
// Versioned document migrations (spec 8.5): each step upgrades a JSON document from version N to
// N+1. Documents are upgraded in memory on load; saving writes the new version and the previous
// file is kept as <file>.bak (fs::write_file_atomic). Newer-than-known versions are refused.

#include "noor/asset/json.h"
#include "noor/foundation/error.h"

#include <functional>
#include <string>
#include <vector>

namespace noor::asset {

class Migrations {
public:
    using Step = std::function<Status(Json& document)>;

    // `version_key` names the document's version field (e.g. "noor_scene").
    Migrations(std::string version_key, int current) : key_(std::move(version_key)), current_(current) {}

    // Registers the step that upgrades version `from` to `from + 1`.
    Migrations& add(int from, std::string description, Step step);

    // Upgrades `document` in place to the current version. Returns the descriptions of the steps
    // applied (empty when it was already current).
    [[nodiscard]] Result<std::vector<std::string>> upgrade(Json& document, std::string_view source) const;

    [[nodiscard]] int current() const noexcept { return current_; }

private:
    struct Entry {
        int from;
        std::string description;
        Step step;
    };
    std::string key_;
    int current_;
    std::vector<Entry> steps_;
};

} // namespace noor::asset
