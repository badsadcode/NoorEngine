#include "noor/scripting/lua_scripts.h"

#include "noor/foundation/file_io.h"
#include "noor/foundation/log.h"
#include "noor/physics/physics.h"
#include "noor/runtime/script_api.h"

#include <lua.h>
#include <luacode.h>
#include <lualib.h>

#include <cmath>
#include <cstdlib>
#include <format>
#include <unordered_map>
#include <unordered_set>

namespace noor::scripting {
namespace {

using asset::Json;
namespace api = runtime::api;
constexpr const char* kEntityMeta = "noor.Entity";

} // namespace

struct LuaScriptSystem::Impl {
    Options options;
    lua_State* L = nullptr;
    runtime::World* world = nullptr;
    double deadline = 0.0;  // lua_clock() time after which the running hook is stopped

    std::unordered_map<std::string, std::string> sources;  // virtual files
    std::unordered_map<std::string, int> modules;          // file -> module table ref (-1: failed)
    struct Instance {
        int ref = 0;
        std::string file;
    };
    std::unordered_map<Guid, Instance> instances;
    std::vector<Guid> order;  // start order, so hooks run in a stable order
    std::vector<std::string> errors;
    std::unordered_set<std::string> reported_load_errors;

    explicit Impl(Options o) : options(std::move(o)) {}
    ~Impl() {
        if (L) lua_close(L);
    }

    static Impl* of(lua_State* L) { return static_cast<Impl*>(lua_callbacks(L)->userdata); }
    static physics::PhysicsSystem* physics_of(lua_State* L) { return world_of(L).system<physics::PhysicsSystem>(); }
    static runtime::World& world_of(lua_State* L) {
        Impl* self = of(L);
        if (!self->world) luaL_errorL(L, "the world is not running");
        return *self->world;
    }

    void report(std::string message) {
        NOOR_LOG_ERROR("Script", "{}", message);
        errors.push_back(std::move(message));
    }

    // ---- Lua <-> JSON ----

    static void push_json(lua_State* L, const Json& j) {
        switch (j.type()) {
        case Json::value_t::null: lua_pushnil(L); break;
        case Json::value_t::boolean: lua_pushboolean(L, j.get<bool>()); break;
        case Json::value_t::number_integer:
        case Json::value_t::number_unsigned:
        case Json::value_t::number_float: lua_pushnumber(L, j.get<double>()); break;
        case Json::value_t::string: {
            const auto& s = j.get_ref<const std::string&>();
            lua_pushlstring(L, s.data(), s.size());
            break;
        }
        case Json::value_t::array: {
            lua_createtable(L, static_cast<int>(j.size()), 0);
            int i = 1;
            for (const auto& v : j) {
                push_json(L, v);
                lua_rawseti(L, -2, i++);
            }
            break;
        }
        case Json::value_t::object: {
            lua_createtable(L, 0, static_cast<int>(j.size()));
            for (const auto& [k, v] : j.items()) {
                push_json(L, v);
                lua_setfield(L, -2, k.c_str());
            }
            break;
        }
        default: lua_pushnil(L); break;
        }
    }

    static Json to_json(lua_State* L, int idx, int depth = 0) {
        if (depth > 32) luaL_errorL(L, "table nested too deeply");
        idx = lua_absindex(L, idx);
        switch (lua_type(L, idx)) {
        case LUA_TNIL: return nullptr;
        case LUA_TBOOLEAN: return lua_toboolean(L, idx) != 0;
        case LUA_TNUMBER: {
            const double d = lua_tonumber(L, idx);
            if (std::floor(d) == d && std::fabs(d) < 9.0e15) return static_cast<std::int64_t>(d);
            return d;
        }
        case LUA_TSTRING: return std::string(lua_tostring(L, idx));
        case LUA_TVECTOR: {
            const float* v = lua_tovector(L, idx);
            return Json::array({v[0], v[1], v[2]});
        }
        case LUA_TUSERDATA:
            if (auto* g = static_cast<Guid*>(luaL_checkudata(L, idx, kEntityMeta))) return g->to_string();
            return nullptr;
        case LUA_TTABLE: {
            const int n = lua_objlen(L, idx);
            bool array = n > 0;
            Json obj = Json::object();
            lua_pushnil(L);
            while (lua_next(L, idx) != 0) {
                if (lua_type(L, -2) == LUA_TSTRING) {
                    array = false;
                    obj[lua_tostring(L, -2)] = to_json(L, -1, depth + 1);
                }
                lua_pop(L, 1);
            }
            if (!array) return obj;
            Json arr = Json::array();
            for (int i = 1; i <= n; ++i) {
                lua_rawgeti(L, idx, i);
                arr.push_back(to_json(L, -1, depth + 1));
                lua_pop(L, 1);
            }
            return arr;
        }
        default: luaL_errorL(L, "can't store a %s in a component", luaL_typename(L, idx));
        }
    }

    // ---- Entities ----

    static void push_entity(lua_State* L, const Guid& id) {
        auto* g = static_cast<Guid*>(lua_newuserdata(L, sizeof(Guid)));
        *g = id;
        luaL_getmetatable(L, kEntityMeta);
        lua_setmetatable(L, -2);
    }
    static Guid& check_entity(lua_State* L, int idx) { return *static_cast<Guid*>(luaL_checkudata(L, idx, kEntityMeta)); }
    static math::Vec3 check_vec(lua_State* L, int idx) {
        const float* v = luaL_checkvector(L, idx);
        return {v[0], v[1], v[2]};
    }
    static int push_vec(lua_State* L, const std::optional<math::Vec3>& v) {
        if (v) lua_pushvector(L, v->x, v->y, v->z);
        else lua_pushnil(L);
        return 1;
    }

    void register_api() {
        // Entity methods.
        static const luaL_Reg entity_methods[] = {
            {"id", [](lua_State* L) {
                 lua_pushstring(L, check_entity(L, 1).to_string().c_str());
                 return 1;
             }},
            {"exists", [](lua_State* L) {
                 lua_pushboolean(L, api::exists(world_of(L), check_entity(L, 1)));
                 return 1;
             }},
            {"name", [](lua_State* L) {
                 if (auto n = api::name(world_of(L), check_entity(L, 1))) lua_pushstring(L, n->c_str());
                 else lua_pushnil(L);
                 return 1;
             }},
            {"child", [](lua_State* L) {
                 if (auto ch = api::child(world_of(L), check_entity(L, 1), luaL_checkstring(L, 2))) push_entity(L, *ch);
                 else lua_pushnil(L);
                 return 1;
             }},
            {"parent", [](lua_State* L) {
                 if (auto p = api::parent(world_of(L), check_entity(L, 1))) push_entity(L, *p);
                 else lua_pushnil(L);
                 return 1;
             }},
            {"position", [](lua_State* L) { return push_vec(L, api::position(world_of(L), check_entity(L, 1))); }},
            {"set_position", [](lua_State* L) {
                 api::set_position(world_of(L), check_entity(L, 1), check_vec(L, 2));
                 return 0;
             }},
            {"translate", [](lua_State* L) {
                 api::translate(world_of(L), check_entity(L, 1), check_vec(L, 2), false);
                 return 0;
             }},
            {"translate_local", [](lua_State* L) {
                 api::translate(world_of(L), check_entity(L, 1), check_vec(L, 2), true);
                 return 0;
             }},
            {"euler", [](lua_State* L) { return push_vec(L, api::euler(world_of(L), check_entity(L, 1))); }},
            {"set_euler", [](lua_State* L) {
                 api::set_euler(world_of(L), check_entity(L, 1), check_vec(L, 2));
                 return 0;
             }},
            {"rotate", [](lua_State* L) {
                 api::rotate(world_of(L), check_entity(L, 1), check_vec(L, 2), static_cast<float>(luaL_checknumber(L, 3)));
                 return 0;
             }},
            {"scale", [](lua_State* L) { return push_vec(L, api::scale(world_of(L), check_entity(L, 1))); }},
            {"set_scale", [](lua_State* L) {
                 api::set_scale(world_of(L), check_entity(L, 1), check_vec(L, 2));
                 return 0;
             }},
            {"world_position", [](lua_State* L) { return push_vec(L, api::world_position(world_of(L), check_entity(L, 1))); }},
            {"forward", [](lua_State* L) { return push_vec(L, api::forward(world_of(L), check_entity(L, 1))); }},
            {"right", [](lua_State* L) { return push_vec(L, api::right(world_of(L), check_entity(L, 1))); }},
            {"up", [](lua_State* L) { return push_vec(L, api::up(world_of(L), check_entity(L, 1))); }},
            {"get", [](lua_State* L) {
                 const Json* c = api::component(world_of(L), check_entity(L, 1), luaL_checkstring(L, 2));
                 if (c) push_json(L, *c);
                 else lua_pushnil(L);
                 return 1;
             }},
            {"set", [](lua_State* L) {
                 api::set_component(world_of(L), check_entity(L, 1), luaL_checkstring(L, 2), to_json(L, 3));
                 return 0;
             }},
            {"destroy", [](lua_State* L) {
                 api::destroy(world_of(L), check_entity(L, 1));
                 return 0;
             }},
            // ---- Physics (needs a collider + rigidbody) ----
            {"velocity", [](lua_State* L) {
                 auto* p = physics_of(L);
                 return push_vec(L, p ? p->velocity(check_entity(L, 1)) : std::nullopt);
             }},
            {"set_velocity", [](lua_State* L) {
                 if (auto* p = physics_of(L)) p->set_velocity(check_entity(L, 1), check_vec(L, 2));
                 return 0;
             }},
            {"add_impulse", [](lua_State* L) {
                 if (auto* p = physics_of(L)) p->add_impulse(check_entity(L, 1), check_vec(L, 2));
                 return 0;
             }},
            {"add_force", [](lua_State* L) {
                 if (auto* p = physics_of(L)) p->add_force(check_entity(L, 1), check_vec(L, 2));
                 return 0;
             }},
            // ---- Character controller (needs a character component) ----
            {"character_move", [](lua_State* L) {
                 if (auto* p = physics_of(L)) p->character_move(check_entity(L, 1), check_vec(L, 2));
                 return 0;
             }},
            {"character_jump", [](lua_State* L) {
                 if (auto* p = physics_of(L)) p->character_jump(check_entity(L, 1), static_cast<float>(luaL_optnumber(L, 2, 5.0)));
                 return 0;
             }},
            {"is_grounded", [](lua_State* L) {
                 auto* p = physics_of(L);
                 lua_pushboolean(L, p && p->is_grounded(check_entity(L, 1)));
                 return 1;
             }},
            {nullptr, nullptr},
        };
        luaL_newmetatable(L, kEntityMeta);
        lua_newtable(L);
        luaL_register(L, nullptr, entity_methods);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, [](lua_State* L) {
            lua_pushboolean(L, check_entity(L, 1) == check_entity(L, 2));
            return 1;
        }, "__eq");
        lua_setfield(L, -2, "__eq");
        lua_pushcfunction(L, [](lua_State* L) {
            const Guid& g = check_entity(L, 1);
            auto n = of(L)->world ? api::name(*of(L)->world, g) : std::nullopt;
            lua_pushstring(L, std::format("Entity({})", n ? *n : g.to_string()).c_str());
            return 1;
        }, "__tostring");
        lua_setfield(L, -2, "__tostring");
        lua_pushstring(L, "noor.Entity");
        lua_setfield(L, -2, "__type");
        lua_pop(L, 1);

        // The `noor` table.
        auto log_with = [](lua_State* L, int level) {
            std::string text;
            const int n = lua_gettop(L);
            for (int i = 1; i <= n; ++i) {
                if (i > 1) text += ' ';
                text += luaL_tolstring(L, i, nullptr);
                lua_pop(L, 1);
            }
            lua_Debug ar{};
            std::string where;
            if (lua_getinfo(L, 1, "sl", &ar) && ar.currentline > 0) where = std::format("{}:{}: ", ar.short_src, ar.currentline);
            if (level == 2) NOOR_LOG_INFO("Script", "{}{}", where, text);
            else NOOR_LOG_WARN("Script", "{}{}", where, text);
            return 0;
        };
        static decltype(log_with) s_log = log_with;
        static const luaL_Reg noor_functions[] = {
            {"log", [](lua_State* L) { return s_log(L, 2); }},
            {"warn", [](lua_State* L) { return s_log(L, 3); }},
            {"time", [](lua_State* L) {
                 lua_pushnumber(L, api::time(world_of(L)));
                 return 1;
             }},
            {"find", [](lua_State* L) {
                 if (auto e = api::find(world_of(L), luaL_checkstring(L, 1))) push_entity(L, *e);
                 else lua_pushnil(L);
                 return 1;
             }},
            // noor.raycast(origin, direction, max_distance [, ignore_entity]) -> hit table or nil
            {"raycast", [](lua_State* L) {
                 auto* p = physics_of(L);
                 const math::Vec3 origin = check_vec(L, 1);
                 const math::Vec3 dir = check_vec(L, 2);
                 const float max = static_cast<float>(luaL_optnumber(L, 3, 1000.0));
                 std::optional<Guid> ignore;
                 if (lua_gettop(L) >= 4 && !lua_isnil(L, 4)) ignore = check_entity(L, 4);
                 const auto hit = p ? p->raycast(origin, dir, max, ignore) : std::nullopt;
                 if (!hit) {
                     lua_pushnil(L);
                     return 1;
                 }
                 lua_createtable(L, 0, 4);
                 push_entity(L, hit->entity);
                 lua_setfield(L, -2, "entity");
                 lua_pushvector(L, hit->point.x, hit->point.y, hit->point.z);
                 lua_setfield(L, -2, "point");
                 lua_pushvector(L, hit->normal.x, hit->normal.y, hit->normal.z);
                 lua_setfield(L, -2, "normal");
                 lua_pushnumber(L, hit->distance);
                 lua_setfield(L, -2, "distance");
                 return 1;
             }},
            {nullptr, nullptr},
        };
        static const luaL_Reg input_functions[] = {
            {"down", [](lua_State* L) {
                 lua_pushboolean(L, api::input_down(world_of(L), luaL_checkstring(L, 1)));
                 return 1;
             }},
            {"pressed", [](lua_State* L) {
                 lua_pushboolean(L, api::input_pressed(world_of(L), luaL_checkstring(L, 1)));
                 return 1;
             }},
            {"released", [](lua_State* L) {
                 lua_pushboolean(L, api::input_released(world_of(L), luaL_checkstring(L, 1)));
                 return 1;
             }},
            {"axis", [](lua_State* L) {
                 lua_pushnumber(L, api::input_axis(world_of(L), luaL_checkstring(L, 1)));
                 return 1;
             }},
            {"capture_mouse", [](lua_State* L) {
                 api::capture_mouse(world_of(L), lua_isnoneornil(L, 1) ? true : lua_toboolean(L, 1) != 0);
                 return 0;
             }},
            {"mouse_captured", [](lua_State* L) {
                 lua_pushboolean(L, api::mouse_captured(world_of(L)));
                 return 1;
             }},
            {nullptr, nullptr},
        };
        lua_newtable(L);
        luaL_register(L, nullptr, noor_functions);
        lua_newtable(L);
        luaL_register(L, nullptr, input_functions);
        lua_setreadonly(L, -1, true);
        lua_setfield(L, -2, "input");
        lua_setreadonly(L, -1, true);
        lua_setglobal(L, "noor");
    }

    void open() {
        L = luaL_newstate();
        lua_callbacks(L)->userdata = this;
        lua_callbacks(L)->interrupt = [](lua_State* L, int gc) {
            if (gc >= 0) return;  // garbage-collector safepoint: not safe to raise errors here
            Impl* self = of(L);
            if (self->deadline > 0.0 && lua_clock() > self->deadline) {
                self->deadline = 0.0;
                luaL_errorL(L, "stopped: the script ran longer than %.2f s (endless loop?)", self->options.call_budget_seconds);
            }
        };
        luaL_openlibs(L);
        register_api();
        luaL_sandbox(L);  // built-in libraries and globals become read-only
    }

    // Loads (once) and returns the module table ref for a script file, or -1.
    int module(const std::string& file) {
        if (const auto it = modules.find(file); it != modules.end()) return it->second;
        modules[file] = -1;
        std::string source;
        if (const auto v = sources.find(file); v != sources.end()) {
            source = v->second;
        } else {
            auto text = fs::read_file(options.project_root / file, 4u << 20);
            if (!text) {
                report(std::format("{}: can't read the script ({})", file, text.error().message));
                return -1;
            }
            source = std::move(*text);
        }
        size_t bytecode_size = 0;
        char* bytecode = luau_compile(source.data(), source.size(), nullptr, &bytecode_size);
        lua_State* T = lua_newthread(L);  // per-file globals
        luaL_sandboxthread(T);
        const std::string chunk = "@" + file;
        const int loaded = luau_load(T, chunk.c_str(), bytecode, bytecode_size, 0);
        std::free(bytecode);
        if (loaded != 0) {
            report(std::format("{}", lua_tostring(T, -1)));  // compile errors carry file:line
            lua_pop(L, 1);
            return -1;
        }
        deadline = lua_clock() + options.call_budget_seconds;
        const int status = lua_pcall(T, 0, 1, 0);
        deadline = 0.0;
        if (status != LUA_OK) {
            report(std::format("{}\n{}", lua_tostring(T, -1), lua_debugtrace(T)));
            lua_pop(L, 1);
            return -1;
        }
        if (!lua_istable(T, -1)) {
            report(std::format("{}: the script must end with `return <table of hooks>`", file));
            lua_pop(L, 1);
            return -1;
        }
        const int ref = lua_ref(T, -1);
        lua_pop(T, 1);
        lua_pop(L, 1);  // the thread (the module table keeps its environment alive)
        modules[file] = ref;
        return ref;
    }

    static int traceback_handler(lua_State* L) {
        luaL_traceback(L, L, lua_tostring(L, 1), 1);
        return 1;
    }

    // Calls instance:hook(args...) if the hook exists. Returns false if it errored (instance stopped).
    bool call(const Guid& id, const char* hook, int nargs_pushed_after) {
        // Stack on entry: the hook's extra arguments (nargs_pushed_after of them).
        const auto it = instances.find(id);
        if (it == instances.end()) {
            lua_pop(L, nargs_pushed_after);
            return true;
        }
        const int base = lua_gettop(L) - nargs_pushed_after;  // arguments sit at base+1..
        lua_pushcfunction(L, traceback_handler, "traceback");
        const int handler = lua_gettop(L);
        lua_getref(L, it->second.ref);
        lua_getfield(L, -1, hook);
        if (!lua_isfunction(L, -1)) {
            lua_settop(L, base);
            return true;
        }
        lua_insert(L, -2);  // function, self
        for (int i = 0; i < nargs_pushed_after; ++i) lua_pushvalue(L, base + 1 + i);
        deadline = lua_clock() + options.call_budget_seconds;
        const int status = lua_pcall(L, 1 + nargs_pushed_after, 0, handler);
        deadline = 0.0;
        if (status != LUA_OK) {
            const std::string message = lua_tostring(L, -1) ? lua_tostring(L, -1) : "error";
            lua_settop(L, base);
            report(std::format("{} ({} on '{}'); the script is stopped for this entity", message, hook,
                               world ? api::name(*world, id).value_or("?") : "?"));
            lua_unref(L, it->second.ref);
            instances.erase(it);
            return false;
        }
        lua_settop(L, base);
        return true;
    }

    void each(const char* hook, double dt) {
        const auto ids = order;  // hooks may destroy entities
        for (const Guid& id : ids) {
            if (!instances.contains(id)) continue;
            lua_pushnumber(L, dt);
            call(id, hook, 1);
        }
        std::erase_if(order, [&](const Guid& id) { return !instances.contains(id); });
    }
};

LuaScriptSystem::LuaScriptSystem(Options options) : impl_(std::make_unique<Impl>(std::move(options))) { impl_->open(); }
LuaScriptSystem::~LuaScriptSystem() = default;

bool LuaScriptSystem::handles(const std::filesystem::path& file) {
    const auto ext = file.extension();
    return ext == ".lua" || ext == ".luau";
}

void LuaScriptSystem::add_source(std::string path, std::string source) { impl_->sources[std::move(path)] = std::move(source); }
const std::vector<std::string>& LuaScriptSystem::errors() const noexcept { return impl_->errors; }
std::size_t LuaScriptSystem::instance_count() const noexcept { return impl_->instances.size(); }

void LuaScriptSystem::entity_started(runtime::World& world, scene::Entity& entity) {
    const auto it = entity.components.find("script");
    if (it == entity.components.end() || !it->is_object()) return;
    const std::string file = it->value("file", std::string{});
    if (file.empty() || !handles(file)) return;
    auto& d = *impl_;
    d.world = &world;
    const int module = d.module(file);
    if (module < 0) return;
    lua_State* L = d.L;
    lua_newtable(L);  // the instance (`self`)
    Impl::push_entity(L, entity.id);
    lua_setfield(L, -2, "entity");
    Impl::push_json(L, it->contains("params") ? (*it)["params"] : Json::object());
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }
    lua_setfield(L, -2, "params");
    lua_newtable(L);  // metatable: methods come from the module
    lua_getref(L, module);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    const Guid id = entity.id;  // `entity` may move if a hook spawns
    d.instances[id] = {lua_ref(L, -1), file};
    lua_pop(L, 1);
    d.order.push_back(id);
    d.call(id, "on_start", 0);
}

void LuaScriptSystem::update(runtime::World& world, double dt) {
    impl_->world = &world;
    impl_->each("on_update", dt);
}

void LuaScriptSystem::fixed_update(runtime::World& world, double dt) {
    impl_->world = &world;
    impl_->each("on_fixed_update", dt);
}

void LuaScriptSystem::late_update(runtime::World& world, double dt) {
    auto& d = *impl_;
    d.world = &world;
    // Physics events from this frame's fixed steps, then the late update.
    for (const auto& e : world.events()) {
        if (!d.instances.contains(e.entity)) continue;
        using K = runtime::Event::Kind;
        const char* hook = e.kind == K::CollisionEnter ? "on_collision_enter"
                         : e.kind == K::CollisionExit  ? "on_collision_exit"
                         : e.kind == K::TriggerEnter   ? "on_trigger_enter"
                                                       : "on_trigger_exit";
        Impl::push_entity(d.L, e.other);
        d.call(e.entity, hook, 1);
    }
    d.each("on_late_update", dt);
}

void LuaScriptSystem::entity_destroyed(runtime::World& world, const scene::Entity& entity) {
    auto& d = *impl_;
    d.world = &world;
    if (!d.instances.contains(entity.id)) return;
    d.call(entity.id, "on_destroy", 0);
    if (const auto it = d.instances.find(entity.id); it != d.instances.end()) {
        lua_unref(d.L, it->second.ref);
        d.instances.erase(it);
    }
}

void LuaScriptSystem::shutdown(runtime::World& world) {
    auto& d = *impl_;
    d.world = &world;
    for (const Guid& id : std::vector<Guid>(d.order)) d.call(id, "on_destroy", 0);
    d.world = nullptr;
}

} // namespace noor::scripting
