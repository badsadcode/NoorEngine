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
// The first entity of that name anywhere below e (a model's bone, 8C part 4: attach props to it, read where it is).
[[nodiscard]] std::optional<Guid> bone(World& w, const Guid& e, std::string_view name);
void destroy(World& w, const Guid& e);  // at the end of the frame, with its children

// ---- Spawning (gameplay foundations, part 1) ----
// A new instance of a prefab (its asset id, or its path like "Assets/Bullet.noorprefab") at `position`, turned by
// `rotation` (world, or local to `parent` if given). Returns the prefab's root entity (the bullet itself: destroying it
// takes everything spawned with it); its entities start next frame, like everything spawned (scripts' on_start, bodies,
// sprites, sounds). Ids are deterministic. Nothing if the prefab can't be found or is empty.
[[nodiscard]] std::optional<Guid> spawn(World& w, std::string_view prefab, math::Vec3 position, math::Quat rotation = {},
                                        std::optional<Guid> parent = std::nullopt);
// Moves an entity at once, without physics in between: a character or body via the physics system (its velocity and
// jump state reset), anything else by its transform. `rotation`: keep the current one if not given.
bool teleport(World& w, const Guid& e, math::Vec3 position, std::optional<math::Quat> rotation = std::nullopt);

// ---- Groups: entity tags (gameplay foundations, part 1; Godot's groups) ----
[[nodiscard]] bool has_tag(World& w, const Guid& e, std::string_view tag);
void add_tag(World& w, const Guid& e, std::string_view tag);
void remove_tag(World& w, const Guid& e, std::string_view tag);
[[nodiscard]] std::vector<Guid> find_all(World& w, std::string_view tag);  // in scene order

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

// ---- Tilemaps (2D part 4): a `tilemap` component's cells, (0, 0) its bottom-left ----
// The tile at (x, y): -1 if empty or outside; nullopt if the entity has no tilemap.
[[nodiscard]] std::optional<std::int32_t> tile(World& w, const Guid& e, int x, int y);
// Sets (x, y) (-1 empties it); false outside the map or without a tilemap. Drawing and collision follow at once.
bool set_tile(World& w, const Guid& e, int x, int y, std::int32_t tile);

// ---- Animations: sprites (Milestone 8B, part 6) and skeletal animators (8C, part 2) ----
// They work on the entity's "animator" component (a model's clips), or else its "sprite" (a sheet's animations); both
// keep the same fields. An animator running a controller: play goes to a state of its base layer, speed and stop scale
// its time, and current_animation is its base layer's state. Plays an animation from its start at `speed` ("animation", "started" and "speed"); a
// non-looping one sends on_animation_end(name) when it has finished. False if the entity has neither component.
bool play_animation(World& w, const Guid& e, std::string_view animation, float speed = 1.0f);
void stop_animation(World& w, const Guid& e);                // freezes it where it is
void set_animation_speed(World& w, const Guid& e, float speed);  // from here on, without jumping (0 freezes it)
[[nodiscard]] std::optional<std::string> current_animation(World& w, const Guid& e);  // what it's playing (or showing)
// Part 3: from what's playing to `animation` over `seconds` (a sprite just plays it). With a controller, `animation`
// is a state of its base layer, as for play_animation.
bool cross_fade(World& w, const Guid& e, std::string_view animation, float seconds, float speed = 1.0f);
// A controller's parameter (a number, or a bool: triggers are bools a transition resets). False without an animator.
bool set_animation_parameter(World& w, const Guid& e, std::string_view name, const asset::Json& value);

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

// ---- Water (Milestone 8D, part 2) ----
// The water surface's height (world y) at world (x, z) now, waves included; nothing where there's no water.
[[nodiscard]] std::optional<float> water_height(World& w, float x, float z);
// Its normal there (up, tilted by the waves).
[[nodiscard]] std::optional<math::Vec3> water_normal(World& w, float x, float z);

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
