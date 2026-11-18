#pragma once
// Instance sets (Milestone 7, part 7): many placements of a few assets (a forest, a rock field), as
// written by procedural graphs (`<output>.instances.json`) and read by games, the editor and any
// other tool. Plain JSON, so a ten-line script can read it (docs/procgraph/PROCGRAPH.md):
//
//   { "noor_instances": 1,
//     "space": {"up": "+Y", "handedness": "right", "units": "meters"},
//     "assets": ["719beb3e-327d-56ed-8d54-be8882f405fd"],
//     "fields": ["asset", "id", "x", "y", "z", "qx", "qy", "qz", "qw", "scale"],
//     "instances": [[0, "9f3c0a1b2c3d4e5f", 12.5, 3.25, -40.0, 0, 0.38, 0, 0.92, 1.07], ...] }
//
// `asset` indexes `assets`; `id` is the instance's stable id (16 hex digits: the same seed and
// settings give the same ids, so hand-made edits can refer to them); position in metres in the
// set's own space (the placing entity's); rotation a unit quaternion; uniform scale. Numbers are
// written so they read back bit for bit.
#include "noor/foundation/error.h"
#include "noor/foundation/guid.h"
#include "noor/foundation/math.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace noor::asset {

inline constexpr int kInstanceSetFormat = 1;

struct InstanceSet {
    struct Item {
        std::uint32_t asset = 0;  // index into assets
        std::uint64_t id = 0;
        math::Vec3 position{};
        math::Quat rotation{};
        float scale = 1.0f;
    };
    std::vector<Guid> assets;
    std::vector<Item> items;

    [[nodiscard]] std::string to_text() const;
    [[nodiscard]] static Result<InstanceSet> from_text(std::string_view text, std::string_view source);
    [[nodiscard]] static Result<InstanceSet> load(const std::filesystem::path& file);
};

// An instance id as written (16 lower-case hex digits) and back.
[[nodiscard]] std::string instance_id_text(std::uint64_t id);
[[nodiscard]] std::optional<std::uint64_t> parse_instance_id(std::string_view text);

} // namespace noor::asset
