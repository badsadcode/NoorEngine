#include "noor/scripting/csharp_scripts.h"

#include "noor/foundation/log.h"
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
}
void rt_character_jump(noor_rt_entity e, float speed) {
    if (auto* p = physics_of()) p->character_jump(from_rt(e), speed);
}
int32_t rt_is_grounded(noor_rt_entity e) {
    auto* p = physics_of();
    return p && p->is_grounded(from_rt(e)) ? 1 : 0;
}

const noor_rt_api kApi = {
    NOOR_RT_API_VERSION, sizeof(noor_rt_api),
    rt_log, rt_time, rt_find,
    rt_exists, rt_name, rt_parent, rt_child, rt_destroy,
    rt_get_vec, rt_set_vec, rt_translate, rt_rotate,
    rt_get_component, rt_set_component,
    rt_input, rt_axis, rt_capture_mouse, rt_mouse_captured,
    rt_raycast, rt_push, rt_character_jump, rt_is_grounded,
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
    order_.push_back(id);
    call(world, id, NOOR_RT_START, 0.0);
}

bool CSharpScriptSystem::call(runtime::World& world, const Guid& id, int hook, double dt) {
    const auto it = instances_.find(id);
    if (it == instances_.end()) return true;
    WorldScope scope(world);
    if (host_->call(it->second, hook, static_cast<float>(dt)) != 0) return true;
    const char* why = host_->last_error ? host_->last_error() : nullptr;
    static constexpr const char* kHooks[] = {"OnStart", "OnUpdate", "OnFixedUpdate", "OnLateUpdate", "OnDestroy"};
    report(std::format("{} ({} on '{}'); the script is stopped for this entity", why ? why : "exception",
                       kHooks[hook], api::name(world, id).value_or("?")));
    release(id);
    return false;
}

void CSharpScriptSystem::each(runtime::World& world, int hook, double dt) {
    const auto ids = order_;
    for (const Guid& id : ids) call(world, id, hook, dt);
    std::erase_if(order_, [&](const Guid& id) { return !instances_.contains(id); });
}

void CSharpScriptSystem::update(runtime::World& world, double dt) { each(world, NOOR_RT_UPDATE, dt); }
void CSharpScriptSystem::fixed_update(runtime::World& world, double dt) { each(world, NOOR_RT_FIXED_UPDATE, dt); }

void CSharpScriptSystem::late_update(runtime::World& world, double dt) {
    for (const auto& e : world.events()) {
        const auto it = instances_.find(e.entity);
        if (it == instances_.end()) continue;
        WorldScope scope(world);
        if (host_->event(it->second, static_cast<int32_t>(e.kind), to_rt(e.other)) == 0) {
            const char* why = host_->last_error ? host_->last_error() : nullptr;
            report(std::format("{} (collision/trigger hook on '{}'); the script is stopped for this entity", why ? why : "exception",
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
