#include "noor/scripting/csharp_scripts.h"

#include "noor/foundation/log.h"
#include "noor/audio/audio.h"
#include "noor/physics/physics.h"
#include "noor/runtime/script_api.h"

#include <cstring>
#include <format>
#include <string>

namespace noor::scripting {
namespace {

namespace api = runtime::api;
using asset::Json;

// The world whose script hook is running (scripts only call back during hooks, on this thread).
thread_local runtime::World* t_world = nullptr;
thread_local std::string t_text;
thread_local std::string t_event_name;  // the event being delivered (AnimationEnd: its animation)

struct WorldScope {
    runtime::World* previous;
    explicit WorldScope(runtime::World& w) : previous(std::exchange(t_world, &w)) {}
    ~WorldScope() { t_world = previous; }
};

math::Vec3 vec(noor_rt_vec3 v) { return {v.x, v.y, v.z}; }
noor_rt_vec3 vec(math::Vec3 v) { return {v.x, v.y, v.z}; }
physics::PhysicsSystem* physics_of() { return t_world ? t_world->system<physics::PhysicsSystem>() : nullptr; }
const char* give(std::string s) {
    t_text = std::move(s);
    return t_text.c_str();
}

// ---- noor_rt_api implementation ----

void rt_log(int32_t level, const char* message) {
    const char* m = message ? message : "";
    if (level >= 4) NOOR_LOG_ERROR("Script", "{}", m);
    else if (level == 3) NOOR_LOG_WARN("Script", "{}", m);
    else NOOR_LOG_INFO("Script", "{}", m);
}
double rt_time() { return t_world ? api::time(*t_world) : 0.0; }
int32_t rt_find(const char* name, noor_rt_entity* out) {
    if (!t_world || !name || !out) return 0;
    const auto e = api::find(*t_world, name);
    if (e) *out = to_rt(*e);
    return e ? 1 : 0;
}
int32_t rt_exists(noor_rt_entity e) { return t_world && api::exists(*t_world, from_rt(e)) ? 1 : 0; }
const char* rt_name(noor_rt_entity e) {
    if (!t_world) return nullptr;
    const auto n = api::name(*t_world, from_rt(e));
    return n ? give(*n) : nullptr;
}
int32_t rt_parent(noor_rt_entity e, noor_rt_entity* out) {
    if (!t_world || !out) return 0;
    const auto p = api::parent(*t_world, from_rt(e));
    if (p) *out = to_rt(*p);
    return p ? 1 : 0;
}
int32_t rt_child(noor_rt_entity e, const char* name, noor_rt_entity* out) {
    if (!t_world || !name || !out) return 0;
    const auto c = api::child(*t_world, from_rt(e), name);
    if (c) *out = to_rt(*c);
    return c ? 1 : 0;
}
void rt_destroy(noor_rt_entity e) {
    if (t_world) api::destroy(*t_world, from_rt(e));
}
int32_t rt_get_vec(noor_rt_entity e, int32_t channel, noor_rt_vec3* out) {
    if (!t_world || !out) return 0;
    auto& w = *t_world;
    const Guid id = from_rt(e);
    std::optional<math::Vec3> v;
    switch (channel) {
    case NOOR_RT_POSITION: v = api::position(w, id); break;
    case NOOR_RT_EULER: v = api::euler(w, id); break;
    case NOOR_RT_SCALE: v = api::scale(w, id); break;
    case NOOR_RT_WORLD_POSITION: v = api::world_position(w, id); break;
    case NOOR_RT_FORWARD: v = api::forward(w, id); break;
    case NOOR_RT_RIGHT: v = api::right(w, id); break;
    case NOOR_RT_UP: v = api::up(w, id); break;
    case NOOR_RT_VELOCITY:
        if (auto* p = physics_of()) v = p->velocity(id);
        break;
    default: break;
    }
    if (v) *out = vec(*v);
    return v ? 1 : 0;
}
void rt_set_vec(noor_rt_entity e, int32_t channel, noor_rt_vec3 v) {
    if (!t_world) return;
    auto& w = *t_world;
    const Guid id = from_rt(e);
    switch (channel) {
    case NOOR_RT_POSITION: api::set_position(w, id, vec(v)); break;
    case NOOR_RT_EULER: api::set_euler(w, id, vec(v)); break;
    case NOOR_RT_SCALE: api::set_scale(w, id, vec(v)); break;
    case NOOR_RT_VELOCITY:
        if (auto* p = physics_of()) p->set_velocity(id, vec(v));
        break;
    default: break;
    }
}
void rt_translate(noor_rt_entity e, noor_rt_vec3 d, int32_t own_axes) {
    if (t_world) api::translate(*t_world, from_rt(e), vec(d), own_axes != 0);
}
void rt_rotate(noor_rt_entity e, noor_rt_vec3 axis, float degrees) {
    if (t_world) api::rotate(*t_world, from_rt(e), vec(axis), degrees);
}
const char* rt_get_component(noor_rt_entity e, const char* type) {
    if (!t_world || !type) return nullptr;
    const Json* c = api::component(*t_world, from_rt(e), type);
    return c ? give(c->dump()) : nullptr;
}
void rt_set_component(noor_rt_entity e, const char* type, const char* json) {
    if (!t_world || !type) return;
    Json value = nullptr;
    if (json) {
        auto parsed = asset::parse_json(json, "script component");
        if (!parsed) {
            NOOR_LOG_ERROR("Script", "set_component(\"{}\"): {}", type, parsed.error().describe());
            return;
        }
        value = std::move(*parsed);
    }
    api::set_component(*t_world, from_rt(e), type, std::move(value));
}
int32_t rt_input(int32_t query, const char* action) {
    if (!t_world || !action) return 0;
    switch (query) {
    case NOOR_RT_DOWN: return api::input_down(*t_world, action) ? 1 : 0;
    case NOOR_RT_PRESSED: return api::input_pressed(*t_world, action) ? 1 : 0;
    case NOOR_RT_RELEASED: return api::input_released(*t_world, action) ? 1 : 0;
    default: return 0;
    }
}
float rt_axis(const char* name) { return t_world && name ? api::input_axis(*t_world, name) : 0.0f; }
void rt_capture_mouse(int32_t capture) {
    if (t_world) api::capture_mouse(*t_world, capture != 0);
}
int32_t rt_mouse_captured() { return t_world && api::mouse_captured(*t_world) ? 1 : 0; }
void rt_raycast(noor_rt_vec3 origin, noor_rt_vec3 dir, float max, const noor_rt_entity* ignore, noor_rt_hit* out) {
    if (!out) return;
    *out = {};
    auto* p = physics_of();
    if (!p) return;
    const auto hit = p->raycast(vec(origin), vec(dir), max, ignore ? std::optional(from_rt(*ignore)) : std::nullopt);
    if (!hit) return;
    *out = {1, to_rt(hit->entity), vec(hit->point), vec(hit->normal), hit->distance};
}
void rt_push(noor_rt_entity e, int32_t kind, noor_rt_vec3 v) {
    auto* p = physics_of();
    if (!p) return;
    const Guid id = from_rt(e);
    if (kind == NOOR_RT_IMPULSE) p->add_impulse(id, vec(v));
    else if (kind == NOOR_RT_FORCE) p->add_force(id, vec(v));
    else if (kind == NOOR_RT_CHARACTER_MOVE) p->character_move(id, vec(v));
    else if (kind == NOOR_RT_CHARACTER_JUMP_RELEASE) p->character_jump_release(id);
    else if (kind == NOOR_RT_CHARACTER_DROP) p->character_drop(id);
}
void rt_character_jump(noor_rt_entity e, float speed) {
    if (auto* p = physics_of()) p->character_jump(from_rt(e), speed);
}
int32_t rt_is_grounded(noor_rt_entity e) {
    auto* p = physics_of();
    return p && p->is_grounded(from_rt(e)) ? 1 : 0;
}

audio::AudioSystem* audio_of() { return t_world ? t_world->system<audio::AudioSystem>() : nullptr; }
int32_t rt_sound(noor_rt_entity e, int32_t op) {
    auto* a = audio_of();
    if (!a) return 0;
    switch (op) {
    case NOOR_RT_SOUND_PLAY: return a->play(from_rt(e)) ? 1 : 0;
    case NOOR_RT_SOUND_STOP: a->stop(from_rt(e)); return 1;
    case NOOR_RT_SOUND_IS_PLAYING: return a->is_playing(from_rt(e)) ? 1 : 0;
    default: return 0;
    }
}
int32_t rt_play_one_shot(const char* clip, const noor_rt_vec3* position, float volume, const char* bus) {
    auto* a = audio_of();
    if (!a || !clip) return 0;
    return a->play_one_shot(clip, position ? std::optional(vec(*position)) : std::nullopt, volume, bus ? bus : "sfx") ? 1 : 0;
}
void rt_set_bus_volume(const char* bus, float volume) {
    if (auto* a = audio_of(); a && bus) a->set_bus_volume(bus, volume);
}

bool ui_style(const char* color, const char* anchor, math::Vec4& c, runtime::UiAnchor& a) {
    if (color) {
        const auto parsed = runtime::api::parse_color(color);
        if (!parsed) return false;
        c = *parsed;
    }
    if (anchor) {
        const auto parsed = runtime::api::parse_anchor(anchor);
        if (!parsed) return false;
        a = *parsed;
    }
    return true;
}
int32_t rt_ui_text(const char* text, float x, float y, float size, const char* color, const char* anchor) {
    math::Vec4 c{1, 1, 1, 1};
    runtime::UiAnchor a = runtime::UiAnchor::TopLeft;
    if (!t_world || !text || !ui_style(color, anchor, c, a)) return 0;
    runtime::api::ui_text(*t_world, text, {x, y}, size, c, a);
    return 1;
}
int32_t rt_ui_rect(float x, float y, float w, float h, const char* color, const char* anchor) {
    math::Vec4 c{1, 1, 1, 1};
    runtime::UiAnchor a = runtime::UiAnchor::TopLeft;
    if (!t_world || !ui_style(color, anchor, c, a)) return 0;
    runtime::api::ui_rect(*t_world, {x, y}, {w, h}, c, a);
    return 1;
}

int32_t rt_sprite(noor_rt_entity e, int32_t op, const char* animation, float speed) {
    if (!t_world) return 0;
    switch (op) {
    case NOOR_RT_SPRITE_PLAY: return animation && api::play_animation(*t_world, from_rt(e), animation, speed) ? 1 : 0;
    case NOOR_RT_SPRITE_STOP: api::stop_animation(*t_world, from_rt(e)); return 1;
    case NOOR_RT_SPRITE_SPEED: api::set_animation_speed(*t_world, from_rt(e), speed); return 1;
    case NOOR_RT_SPRITE_CROSS_FADE: return animation && api::cross_fade(*t_world, from_rt(e), animation, speed) ? 1 : 0;
    default: return 0;
    }
}
const char* rt_sprite_animation(noor_rt_entity e) {
    if (!t_world) return nullptr;
    const auto a = api::current_animation(*t_world, from_rt(e));
    return a ? give(*a) : nullptr;
}
int32_t rt_burst(noor_rt_entity e, int32_t count) { return t_world && api::emitter_burst(*t_world, from_rt(e), count) ? 1 : 0; }
const char* rt_event_name() { return t_event_name.c_str(); }
int32_t rt_bone(noor_rt_entity e, const char* name, noor_rt_entity* out) {
    if (!t_world || !name || !out) return 0;
    const auto b = api::bone(*t_world, from_rt(e), name);
    if (b) *out = to_rt(*b);
    return b ? 1 : 0;
}
int32_t rt_animation_parameter(noor_rt_entity e, const char* name, float value, int32_t kind) {
    if (!t_world || !name) return 0;
    const asset::Json v = kind == NOOR_RT_PARAM_BOOL ? asset::Json(value != 0.0f) : asset::Json(value);
    return api::set_animation_parameter(*t_world, from_rt(e), name, v) ? 1 : 0;
}

int32_t rt_water(float x, float z, float* height, noor_rt_vec3* normal) {
    if (!t_world) return 0;
    const auto s = t_world->water_at(x, z);
    if (!s) return 0;
    if (height) *height = s->height;
    if (normal) *normal = {s->normal.x, s->normal.y, s->normal.z};
    return 1;
}

int32_t rt_character_water(noor_rt_entity e, float* feet, float* head) {
    auto* p = physics_of();
    const auto w = p ? p->character_water(from_rt(e)) : std::nullopt;
    if (!w) return -1;
    if (feet) *feet = w->feet_depth;
    if (head) *head = w->head_depth;
    return static_cast<int32_t>(w->state);
}

void rt_shape_cast(const char* shape_json, noor_rt_vec3 from, noor_rt_vec3 to, const noor_rt_entity* ignore, noor_rt_hit* out) {
    if (!out) return;
    *out = {};
    auto* p = physics_of();
    if (!p || !shape_json) return;
    const Json shape = Json::parse(shape_json, nullptr, false);
    const auto hit = p->shape_cast(shape, vec(from), vec(to), ignore ? std::optional(from_rt(*ignore)) : std::nullopt);
    if (!hit) return;
    *out = {1, to_rt(hit->entity), vec(hit->point), vec(hit->normal), hit->distance};
}
int32_t rt_overlap(const char* shape_json, noor_rt_vec3 at, const noor_rt_entity* ignore, noor_rt_entity* out, int32_t max) {
    auto* p = physics_of();
    if (!p || !shape_json) return 0;
    const Json shape = Json::parse(shape_json, nullptr, false);
    const auto found = p->overlap(shape, vec(at), ignore ? std::optional(from_rt(*ignore)) : std::nullopt);
    for (std::size_t i = 0; out && i < found.size() && static_cast<int32_t>(i) < max; ++i) out[i] = to_rt(found[i]);
    return static_cast<int32_t>(found.size());
}
int32_t rt_character_contacts(noor_rt_entity e, noor_rt_vec3* floor_normal) {
    auto* p = physics_of();
    const auto c = p ? p->character_contacts(from_rt(e)) : std::nullopt;
    if (!c) return -1;
    if (floor_normal) *floor_normal = vec(c->floor_normal);
    return (c->floor ? 1 : 0) | (c->wall ? 2 : 0) | (c->ceiling ? 4 : 0);
}

int32_t rt_tile(noor_rt_entity e, int32_t x, int32_t y) {
    if (!t_world) return -2;
    return runtime::api::tile(*t_world, from_rt(e), x, y).value_or(-2);
}
int32_t rt_set_tile(noor_rt_entity e, int32_t x, int32_t y, int32_t tile) {
    return t_world && runtime::api::set_tile(*t_world, from_rt(e), x, y, tile) ? 1 : 0;
}

int32_t rt_spawn(const char* prefab, noor_rt_vec3 position, noor_rt_vec3 euler, const noor_rt_entity* parent, noor_rt_entity* out) {
    if (!t_world || !prefab) return 0;
    const auto e = api::spawn(*t_world, prefab, vec(position), api::quat_from_euler(vec(euler)),
                              parent ? std::optional(from_rt(*parent)) : std::nullopt);
    if (e && out) *out = to_rt(*e);
    return e ? 1 : 0;
}
int32_t rt_teleport(noor_rt_entity e, noor_rt_vec3 position, const noor_rt_vec3* euler) {
    if (!t_world) return 0;
    return api::teleport(*t_world, from_rt(e), vec(position), euler ? std::optional(api::quat_from_euler(vec(*euler))) : std::nullopt) ? 1 : 0;
}
int32_t rt_tag(noor_rt_entity e, int32_t op, const char* tag) {
    if (!t_world || !tag) return 0;
    switch (op) {
    case NOOR_RT_TAG_HAS: return api::has_tag(*t_world, from_rt(e), tag) ? 1 : 0;
    case NOOR_RT_TAG_ADD: api::add_tag(*t_world, from_rt(e), tag); return 1;
    case NOOR_RT_TAG_REMOVE: api::remove_tag(*t_world, from_rt(e), tag); return 1;
    default: return 0;
    }
}
int32_t rt_find_all(const char* tag, noor_rt_entity* out, int32_t max) {
    if (!t_world || !tag) return 0;
    const auto all = api::find_all(*t_world, tag);
    for (std::size_t i = 0; out && i < all.size() && static_cast<int32_t>(i) < max; ++i) out[i] = to_rt(all[i]);
    return static_cast<int32_t>(all.size());
}

double rt_clock(int32_t which) {
    if (!t_world) return 0.0;
    switch (which) {
    case NOOR_RT_CLOCK_GAME: return t_world->time();
    case NOOR_RT_CLOCK_REAL: return t_world->real_time();
    case NOOR_RT_CLOCK_SCALE: return t_world->time_scale();
    case NOOR_RT_CLOCK_PAUSED: return t_world->paused() ? 1.0 : 0.0;
    default: return 0.0;
    }
}
void rt_set_clock(int32_t which, double value) {
    if (!t_world) return;
    if (which == NOOR_RT_CLOCK_SCALE) t_world->set_time_scale(value);
    else if (which == NOOR_RT_CLOCK_PAUSED) t_world->set_paused(value != 0.0);
}

int32_t rt_water_flow(float x, float z, noor_rt_vec3* flow) {
    if (!t_world) return 0;
    const auto s = t_world->water_at(x, z);
    if (!s) return 0;
    if (flow) *flow = {s->flow.x, s->flow.y, s->flow.z};
    return 1;
}

const noor_rt_api kApi = {
    NOOR_RT_API_VERSION, sizeof(noor_rt_api),
    rt_log, rt_time, rt_find,
    rt_exists, rt_name, rt_parent, rt_child, rt_destroy,
    rt_get_vec, rt_set_vec, rt_translate, rt_rotate,
    rt_get_component, rt_set_component,
    rt_input, rt_axis, rt_capture_mouse, rt_mouse_captured,
    rt_raycast, rt_push, rt_character_jump, rt_is_grounded,
    rt_sound, rt_play_one_shot, rt_set_bus_volume,
    rt_ui_text, rt_ui_rect,
    rt_sprite, rt_sprite_animation, rt_burst, rt_event_name,
    rt_animation_parameter,
    rt_bone,
    rt_water,
    rt_character_water,
    rt_water_flow,
    rt_shape_cast, rt_overlap, rt_character_contacts,
    rt_tile, rt_set_tile,
    rt_spawn, rt_teleport, rt_tag, rt_find_all,
    rt_clock, rt_set_clock,
};

} // namespace

const noor_rt_api* rt_api() { return &kApi; }

noor_rt_entity to_rt(const Guid& id) {
    noor_rt_entity e;
    std::memcpy(&e.lo, id.bytes.data(), 8);
    std::memcpy(&e.hi, id.bytes.data() + 8, 8);
    return e;
}

Guid from_rt(const noor_rt_entity& e) {
    Guid g;
    std::memcpy(g.bytes.data(), &e.lo, 8);
    std::memcpy(g.bytes.data() + 8, &e.hi, 8);
    return g;
}

CSharpScriptSystem::~CSharpScriptSystem() {
    for (const auto& [id, handle] : instances_)
        if (host_) host_->release(handle);
}

void CSharpScriptSystem::report(std::string message) {
    NOOR_LOG_ERROR("Script", "{}", message);
    errors_.push_back(std::move(message));
}

void CSharpScriptSystem::release(const Guid& id) {
    if (const auto it = instances_.find(id); it != instances_.end()) {
        if (host_) host_->release(it->second);
        instances_.erase(it);
        always_.erase(id);
    }
}

void CSharpScriptSystem::entity_started(runtime::World& world, scene::Entity& entity) {
    const auto it = entity.components.find("script");
    if (it == entity.components.end() || !it->is_object()) return;
    const std::string file = it->value("file", std::string{});
    if (file.empty() || !handles(file)) return;
    if (!host_) {
        if (!reported_missing_host_) report(std::format("{}: C# scripts need the .NET script host (were the scripts compiled?)", file));
        reported_missing_host_ = true;
        return;
    }
    const std::string class_name = std::filesystem::path(file).stem().string();
    const std::string params = it->contains("params") ? (*it)["params"].dump() : "{}";
    const Guid id = entity.id;
    WorldScope scope(world);
    const std::int64_t handle = host_->create(to_rt(id), class_name.c_str(), params.c_str());
    if (handle == 0) {
        const char* why = host_->last_error ? host_->last_error() : nullptr;
        report(std::format("{} on '{}': {}", file, entity.name, why ? why : "could not be created"));
        return;
    }
    instances_[id] = handle;
    if (it->value("pause", std::string()) == "always") always_.insert(id);
    order_.push_back(id);
    call(world, id, NOOR_RT_START, 0.0);
}

bool CSharpScriptSystem::call(runtime::World& world, const Guid& id, int hook, double dt) {
    const auto it = instances_.find(id);
    if (it == instances_.end()) return true;
    WorldScope scope(world);
    world.set_drawing_while_paused(always_.contains(id));
    const bool ok = host_->call(it->second, hook, static_cast<float>(dt)) != 0;
    world.set_drawing_while_paused(false);
    if (ok) return true;
    const char* why = host_->last_error ? host_->last_error() : nullptr;
    static constexpr const char* kHooks[] = {"OnStart", "OnUpdate", "OnFixedUpdate", "OnLateUpdate", "OnDestroy", "a timer or wait"};
    report(std::format("{} ({} on '{}'); the script is stopped for this entity", why ? why : "exception",
                       kHooks[hook], api::name(world, id).value_or("?")));
    release(id);
    return false;
}

void CSharpScriptSystem::each(runtime::World& world, int hook, double dt) {
    const auto ids = order_;
    // Paused (gameplay foundations, part 2): only scripts marked "pause": "always", on the real clock.
    const bool paused = world.paused();
    if (paused) dt = world.real_dt();
    for (const Guid& id : ids)
        if (!paused || always_.contains(id)) call(world, id, hook, dt);
    std::erase_if(order_, [&](const Guid& id) { return !instances_.contains(id); });
}

void CSharpScriptSystem::update(runtime::World& world, double dt) {
    // Timers and waits first, every script's, as Lua does: a pause menu's "resume" timer lets the other scripts update
    // this same frame (gameplay foundations, part 2). Paused, only the scripts that run while paused.
    for (const Guid& id : std::vector<Guid>(order_))
        if (!world.paused() || always_.contains(id)) call(world, id, NOOR_RT_JOBS, dt);
    each(world, NOOR_RT_UPDATE, dt);
}
void CSharpScriptSystem::fixed_update(runtime::World& world, double dt) { each(world, NOOR_RT_FIXED_UPDATE, dt); }

void CSharpScriptSystem::late_update(runtime::World& world, double dt) {
    for (const auto& e : world.events()) {
        const auto it = instances_.find(e.entity);
        if (it == instances_.end()) continue;
        WorldScope scope(world);
        t_event_name = e.name;
        if (host_->event(it->second, static_cast<int32_t>(e.kind), to_rt(e.other)) == 0) {
            const char* why = host_->last_error ? host_->last_error() : nullptr;
            report(std::format("{} (event hook on '{}'); the script is stopped for this entity", why ? why : "exception",
                               api::name(world, e.entity).value_or("?")));
            release(e.entity);
        }
    }
    each(world, NOOR_RT_LATE_UPDATE, dt);
}

void CSharpScriptSystem::entity_destroyed(runtime::World& world, const scene::Entity& entity) {
    if (!instances_.contains(entity.id)) return;
    call(world, entity.id, NOOR_RT_DESTROY, 0.0);
    release(entity.id);
}

void CSharpScriptSystem::shutdown(runtime::World& world) {
    for (const Guid& id : std::vector<Guid>(order_)) {
        call(world, id, NOOR_RT_DESTROY, 0.0);
        release(id);
    }
    order_.clear();
}

} // namespace noor::scripting
