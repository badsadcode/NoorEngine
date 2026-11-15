#pragma once
// The scripting API (ADR-027): everything a script may do to the world, in one place.
// Lua (engine/scripting) binds these functions; C# (Milestone 4, part 4) wraps the same list as
// noor_rt_* C functions. A feature exists for scripts once it exists here, for both languages.
//
// Conventions: positions in metres, angles in degrees, Euler angles as the editor shows them
// (pitch X, yaw Y, roll Z; q = yaw * pitch * roll). Transforms are local (relative to the parent)
// unless a function says "world". Functions taking an entity id do nothing / return nothing for
// ids that no longer exist (an entity may be destroyed while a script still holds it).

#include "noor/runtime/world.h"

#include <optional>
#include <string>
#include <string_view>

namespace noor::runtime::api {

// ---- Entities ----
[[nodiscard]] bool exists(World& w, const Guid& e);
[[nodiscard]] std::optional<std::string> name(World& w, const Guid& e);
[[nodiscard]] std::optional<Guid> find(World& w, std::string_view name);  // first match
[[nodiscard]] std::optional<Guid> parent(World& w, const Guid& e);
void destroy(World& w, const Guid& e);  // at the end of the frame, with its children

// ---- Transform (local) ----
[[nodiscard]] std::optional<math::Vec3> position(World& w, const Guid& e);
void set_position(World& w, const Guid& e, math::Vec3 p);
void translate(World& w, const Guid& e, math::Vec3 delta, bool in_own_space);  // own space: along the entity's axes
[[nodiscard]] std::optional<math::Vec3> euler(World& w, const Guid& e);
void set_euler(World& w, const Guid& e, math::Vec3 degrees);
void rotate(World& w, const Guid& e, math::Vec3 axis, float degrees);  // about an axis in parent space
[[nodiscard]] std::optional<math::Vec3> scale(World& w, const Guid& e);
void set_scale(World& w, const Guid& e, math::Vec3 s);

// ---- Directions (world space) ----
[[nodiscard]] std::optional<math::Vec3> world_position(World& w, const Guid& e);
[[nodiscard]] std::optional<math::Vec3> forward(World& w, const Guid& e);  // the entity's -Z
[[nodiscard]] std::optional<math::Vec3> right(World& w, const Guid& e);    // +X
[[nodiscard]] std::optional<math::Vec3> up(World& w, const Guid& e);       // +Y

// ---- Components (JSON data) ----
[[nodiscard]] const asset::Json* component(World& w, const Guid& e, std::string_view type);
void set_component(World& w, const Guid& e, std::string_view type, asset::Json value);  // null removes

// ---- Input, time ----
[[nodiscard]] bool input_down(World& w, std::string_view action);
[[nodiscard]] bool input_pressed(World& w, std::string_view action);
[[nodiscard]] bool input_released(World& w, std::string_view action);
[[nodiscard]] float input_axis(World& w, std::string_view axis);
[[nodiscard]] double time(World& w);

// ---- Euler helpers (shared with the editor's convention) ----
[[nodiscard]] math::Quat quat_from_euler(math::Vec3 degrees);
[[nodiscard]] math::Vec3 euler_from_quat(math::Quat q);

} // namespace noor::runtime::api
