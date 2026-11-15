#pragma once
// Internal to noor::scene: entity <-> JSON in the current scene format, shared by scenes and prefabs.

#include "noor/scene/scene.h"

namespace noor::scene::detail {

[[nodiscard]] Result<Entity> parse_entity(const asset::Json& j, std::size_t index, std::string_view source);
[[nodiscard]] asset::Json write_entity(const Entity& e);

} // namespace noor::scene::detail
