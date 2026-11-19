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
[[nodiscard]] std::optional<Guid> child(World& w, const Guid& e, std::string_view name);  // first direct child
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

// ---- World space ----
[[nodiscard]] std::optional<math::Mat4> world_matrix(World& w, const Guid& e);
[[nodiscard]] std::optional<math::Quat> world_rotation(World& w, const Guid& e);
// Sets the entity's local transform so that its world position/rotation become these.
void set_world_pose(World& w, const Guid& e, math::Vec3 position, math::Quat rotation);

// ---- Directions (world space) ----
[[nodiscard]] std::optional<math::Vec3> world_position(World& w, const Guid& e);
[[nodiscard]] std::optional<math::Vec3> forward(World& w, const Guid& e);  // the entity's -Z
[[nodiscard]] std::optional<math::Vec3> right(World& w, const Guid& e);    // +X
[[nodiscard]] std::optional<math::Vec3> up(World& w, const Guid& e);       // +Y

// ---- Components (JSON data) ----
[[nodiscard]] const asset::Json* component(World& w, const Guid& e, std::string_view type);
void set_component(World& w, const Guid& e, std::string_view type, asset::Json value);  // null removes

// ---- Sprites (Milestone 8B, part 6) ----
// Plays an animation from its start at `speed` (the component's "animation", "started" and "speed"); a non-looping
// one sends on_animation_end(name) when it has finished. False if the entity has no sprite component.
bool sprite_play(World& w, const Guid& e, std::string_view animation, float speed = 1.0f);
void sprite_stop(World& w, const Guid& e);                // freezes it on the frame it's showing
void sprite_set_speed(World& w, const Guid& e, float speed);  // from here on, without jumping (0 freezes it)
[[nodiscard]] std::optional<std::string> sprite_animation(World& w, const Guid& e);  // what it's playing (or showing)

// ---- Effects (Milestone 8B, part 6) ----
// `count` particles from the entity's emitter at the next fixed step. False if nothing handles emitters.
// (An emitter's settings are changed with set_component: its particles carry on.)
bool emitter_burst(World& w, const Guid& e, int count);

// ---- Input, time ----
[[nodiscard]] bool input_down(World& w, std::string_view action);
[[nodiscard]] bool input_pressed(World& w, std::string_view action);
[[nodiscard]] bool input_released(World& w, std::string_view action);
[[nodiscard]] float input_axis(World& w, std::string_view axis);
[[nodiscard]] double time(World& w);
void capture_mouse(World& w, bool capture);
[[nodiscard]] bool mouse_captured(World& w);

// ---- HUD (UI units: the screen is 720 tall; see UiCommand) ----
// "top_left", "top", "top_right", "left", "center", "right", "bottom_left", "bottom", "bottom_right".
[[nodiscard]] std::optional<UiAnchor> parse_anchor(std::string_view name);
// "#RRGGBB" or "#RRGGBBAA" (sRGB, as colour pickers show it) -> linear RGBA.
[[nodiscard]] std::optional<math::Vec4> parse_color(std::string_view hex);
void ui_text(World& w, std::string text, math::Vec2 position, float size, math::Vec4 color, UiAnchor anchor);
void ui_rect(World& w, math::Vec2 position, math::Vec2 size, math::Vec4 color, UiAnchor anchor);

// ---- Euler helpers (shared with the editor's convention) ----
[[nodiscard]] math::Quat quat_from_euler(math::Vec3 degrees);
[[nodiscard]] math::Vec3 euler_from_quat(math::Quat q);

} // namespace noor::runtime::api
