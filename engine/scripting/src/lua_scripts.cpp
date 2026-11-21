#include "noor/scripting/lua_scripts.h"

#include "noor/foundation/file_io.h"
#include "noor/foundation/log.h"
#include "noor/audio/audio.h"
#include "noor/physics/physics.h"
#include "noor/runtime/script_api.h"

#include <lua.h>
#include <luacode.h>
#include <lualib.h>

#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <format>
#include <map>
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
    std::unordered_set<std::string> loading;               // files being loaded now (a require cycle)
    struct Instance {
        int ref = 0;
        std::string file;
        bool always = false;  // "pause": "always": runs while the game is paused (gameplay foundations, part 2)
    };
    // Timers and waiting sequences (gameplay foundations, part 2), each belonging to the script instance that made it
    // (gone with its entity), in the order they were made (so they run in a stable order).
    Guid current;  // the instance whose code is running now (nil outside hooks)
    std::uint64_t next_job = 1;
    struct Timer {
        Guid owner;
        double due = 0.0, period = 0.0;  // period 0: once
        bool real = false;               // on the real clock (runs while paused, for "always" scripts)
        int fn = 0;
    };
    std::map<std::uint64_t, Timer> timers;
    struct Sequence {
        Guid owner;
        lua_State* thread = nullptr;
        int ref = 0;  // keeps the thread alive
        double due = -1.0;  // waiting until then (-1: not started yet)
        bool real = false;
    };
    std::map<std::uint64_t, Sequence> sequences;
    std::unordered_map<lua_State*, std::uint64_t> sequence_of;  // a running sequence's thread -> its id
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
    static audio::AudioSystem* audio_of(lua_State* L) { return world_of(L).system<audio::AudioSystem>(); }
    static runtime::World& world_of(lua_State* L) {
        Impl* self = of(L);
        if (!self->world) luaL_errorL(L, "the world is not running");
        return *self->world;
    }

    void report(std::string message) {
        NOOR_LOG_ERROR("Script", "{}", message);
        errors.push_back(std::move(message));
    }
    std::unordered_set<std::string> warned;
    void warn_once(std::string message) {
        if (warned.insert(message).second) NOOR_LOG_WARN("Script", "{}", message);
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
    // The optional table of noor.ui.* calls: size, color ("#RRGGBB[AA]"), anchor ("top_left", ...).
    struct UiOptions {
        float size = 24.0f;
        math::Vec4 color{1, 1, 1, 1};
        runtime::UiAnchor anchor = runtime::UiAnchor::TopLeft;
    };
    static UiOptions ui_options(lua_State* L, int idx) {
        UiOptions o;
        if (lua_isnoneornil(L, idx)) return o;
        luaL_checktype(L, idx, LUA_TTABLE);
        lua_getfield(L, idx, "size");
        if (!lua_isnil(L, -1)) o.size = static_cast<float>(luaL_checknumber(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, idx, "color");
        if (!lua_isnil(L, -1)) {
            const char* c = luaL_checkstring(L, -1);
            const auto parsed = runtime::api::parse_color(c);
            if (!parsed) luaL_error(L, "color '%s': use \"#RRGGBB\" or \"#RRGGBBAA\"", c);
            o.color = *parsed;
        }
        lua_pop(L, 1);
        lua_getfield(L, idx, "anchor");
        if (!lua_isnil(L, -1)) {
            const char* a = luaL_checkstring(L, -1);
            const auto parsed = runtime::api::parse_anchor(a);
            if (!parsed) luaL_error(L, "anchor '%s': top_left, top, top_right, left, center, right, bottom_left, bottom or bottom_right", a);
            o.anchor = *parsed;
        }
        lua_pop(L, 1);
        return o;
    }
    static int push_vec(lua_State* L, const std::optional<math::Vec3>& v) {
        if (v) lua_pushvector(L, v->x, v->y, v->z);
        else lua_pushnil(L);
        return 1;
    }

    // require("util") / require("Scripts/util.luau") (gameplay foundations, part 1): another script file's table, loaded
    // once and shared (the same table for every script that requires it). Project-relative, inside the project.
    static int lua_require(lua_State* L) {
        Impl* self = of(L);
        std::string name = luaL_checkstring(L, 1);
        std::replace(name.begin(), name.end(), '\\', '/');
        if (name.empty() || name.front() == '/' || name.find(':') != std::string::npos || name.find("..") != std::string::npos)
            luaL_errorL(L, "require: '%s' must be a path inside the project, like \"Scripts/util.luau\" or \"util\"", name.c_str());
        if (!name.ends_with(".luau") && !name.ends_with(".lua")) name += ".luau";
        std::error_code ec;
        const auto known = [&](const std::string& f) { return self->sources.contains(f) || std::filesystem::exists(self->options.project_root / f, ec); };
        std::string file = name;
        if (!known(file) && known("Scripts/" + name)) file = "Scripts/" + name;
        if (self->loading.contains(file)) luaL_errorL(L, "require: '%s' requires itself (a cycle)", file.c_str());
        self->loading.insert(file);
        const int ref = self->module(file);
        self->loading.erase(file);
        if (ref < 0) {
            // Say why (the first line of what went wrong loading it: a syntax error, a cycle further down...).
            std::string why = self->errors.empty() ? std::string("see the Console") : self->errors.back();
            why = why.substr(0, why.find('\n'));
            luaL_errorL(L, "require: '%s' couldn't be loaded: %s", file.c_str(), why.c_str());
        }
        lua_getref(L, ref);
        return 1;
    }

    // noor.after / noor.every: a timer owned by the running script instance.
    static int add_timer(lua_State* L, bool repeat) {
        Impl* self = of(L);
        const double seconds = luaL_checknumber(L, 1);
        luaL_checktype(L, 2, LUA_TFUNCTION);
        if (self->current == Guid{} || !self->instances.contains(self->current))
            luaL_errorL(L, "noor.%s: call it from a script's hook (a timer belongs to its entity)", repeat ? "every" : "after");
        if (repeat && seconds <= 0.0) luaL_errorL(L, "noor.every: the period must be more than 0 seconds");
        bool real = false;
        if (lua_istable(L, 3)) {
            lua_getfield(L, 3, "real");
            real = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
        }
        lua_pushvalue(L, 2);
        const int fn = lua_ref(L, -1);
        lua_pop(L, 1);
        const std::uint64_t id = self->next_job++;
        self->timers[id] = {self->current, self->clock(real) + std::max(0.0, seconds), repeat ? seconds : 0.0, real, fn};
        auto* handle = static_cast<std::uint64_t*>(lua_newuserdata(L, sizeof(std::uint64_t)));
        *handle = id;
        luaL_getmetatable(L, "noor.Timer");
        lua_setmetatable(L, -2);
        return 1;
    }

    void register_api() {
        luaL_newmetatable(L, "noor.Timer");  // t:cancel()
        lua_newtable(L);
        lua_pushcfunction(L, [](lua_State* L) {
            Impl* self = of(L);
            const auto id = *static_cast<std::uint64_t*>(luaL_checkudata(L, 1, "noor.Timer"));
            if (const auto it = self->timers.find(id); it != self->timers.end()) {
                lua_unref(L, it->second.fn);
                self->timers.erase(it);
            }
            return 0;
        }, "cancel");
        lua_setfield(L, -2, "cancel");
        lua_setfield(L, -2, "__index");
        lua_pop(L, 1);
        lua_pushcfunction(L, lua_require, "require");
        lua_setglobal(L, "require");
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
            {"bone", [](lua_State* L) {
                 if (auto b = api::bone(world_of(L), check_entity(L, 1), luaL_checkstring(L, 2))) push_entity(L, *b);
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
            // e:teleport(position [, euler]): at once, velocity and jumps reset (gameplay foundations, part 1)
            {"teleport", [](lua_State* L) {
                 std::optional<math::Quat> rot;
                 if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) rot = api::quat_from_euler(check_vec(L, 3));
                 lua_pushboolean(L, api::teleport(world_of(L), check_entity(L, 1), check_vec(L, 2), rot));
                 return 1;
             }},
            // Groups (tags)
            {"has_tag", [](lua_State* L) {
                 lua_pushboolean(L, api::has_tag(world_of(L), check_entity(L, 1), luaL_checkstring(L, 2)));
                 return 1;
             }},
            {"add_tag", [](lua_State* L) {
                 api::add_tag(world_of(L), check_entity(L, 1), luaL_checkstring(L, 2));
                 return 0;
             }},
            {"remove_tag", [](lua_State* L) {
                 api::remove_tag(world_of(L), check_entity(L, 1), luaL_checkstring(L, 2));
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
            // ---- Sound (needs an audio_source component) ----
            {"play_sound", [](lua_State* L) {
                 auto* a = audio_of(L);
                 lua_pushboolean(L, a && a->play(check_entity(L, 1)));
                 return 1;
             }},
            {"stop_sound", [](lua_State* L) {
                 if (auto* a = audio_of(L)) a->stop(check_entity(L, 1));
                 return 0;
             }},
            {"is_playing", [](lua_State* L) {
                 auto* a = audio_of(L);
                 lua_pushboolean(L, a && a->is_playing(check_entity(L, 1)));
                 return 1;
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
            {"character_jump_release", [](lua_State* L) {  // 2D part 2: the jump button let go (a shorter jump)
                 if (auto* p = physics_of(L)) p->character_jump_release(check_entity(L, 1));
                 return 0;
             }},
            {"character_drop", [](lua_State* L) {  // through a one-way platform
                 if (auto* p = physics_of(L)) p->character_drop(check_entity(L, 1));
                 return 0;
             }},
            // Godot-style (2D part 2b): what the character touched in its last step.
            {"is_on_floor", [](lua_State* L) {
                 auto* p = physics_of(L);
                 const auto c = p ? p->character_contacts(check_entity(L, 1)) : std::nullopt;
                 lua_pushboolean(L, c && c->floor);
                 return 1;
             }},
            {"is_on_wall", [](lua_State* L) {
                 auto* p = physics_of(L);
                 const auto c = p ? p->character_contacts(check_entity(L, 1)) : std::nullopt;
                 lua_pushboolean(L, c && c->wall);
                 return 1;
             }},
            {"is_on_ceiling", [](lua_State* L) {
                 auto* p = physics_of(L);
                 const auto c = p ? p->character_contacts(check_entity(L, 1)) : std::nullopt;
                 lua_pushboolean(L, c && c->ceiling);
                 return 1;
             }},
            {"floor_normal", [](lua_State* L) {
                 auto* p = physics_of(L);
                 const auto c = p ? p->character_contacts(check_entity(L, 1)) : std::nullopt;
                 return push_vec(L, c && c->floor ? std::optional(c->floor_normal) : std::nullopt);
             }},
            // Tilemaps (2D part 4): e:tile(x, y) -> the tile number there (-1 empty), nil without a tilemap;
            // e:set_tile(x, y, tile) (-1 or nil empties it) -> true if it was inside the map.
            {"tile", [](lua_State* L) {
                 const auto t = api::tile(world_of(L), check_entity(L, 1), static_cast<int>(luaL_checkinteger(L, 2)),
                                          static_cast<int>(luaL_checkinteger(L, 3)));
                 if (t) lua_pushinteger(L, *t);
                 else lua_pushnil(L);
                 return 1;
             }},
            {"set_tile", [](lua_State* L) {
                 const auto t = lua_isnoneornil(L, 4) ? -1 : static_cast<std::int32_t>(luaL_checkinteger(L, 4));
                 lua_pushboolean(L, api::set_tile(world_of(L), check_entity(L, 1), static_cast<int>(luaL_checkinteger(L, 2)),
                                                  static_cast<int>(luaL_checkinteger(L, 3)), t));
                 return 1;
             }},
            {"is_grounded", [](lua_State* L) {
                 auto* p = physics_of(L);
                 lua_pushboolean(L, p && p->is_grounded(check_entity(L, 1)));
                 return 1;
             }},
            // In the water (8D part 5): state ("dry", "wading", "swimming", "under"), depth at the feet, at the head.
            {"character_water", [](lua_State* L) {
                 auto* p = physics_of(L);
                 const auto w = p ? p->character_water(check_entity(L, 1)) : std::nullopt;
                 if (!w) {
                     lua_pushnil(L);
                     return 1;
                 }
                 lua_pushstring(L, physics::PhysicsSystem::water_state_name(w->state));
                 lua_pushnumber(L, w->feet_depth);
                 lua_pushnumber(L, w->head_depth);
                 return 3;
             }},
            // ---- Animations (an animator or a sprite component; 8B part 6, 8C part 2) ----
            {"play_animation", [](lua_State* L) {
                 lua_pushboolean(L, api::play_animation(world_of(L), check_entity(L, 1), luaL_checkstring(L, 2),
                                                     static_cast<float>(luaL_optnumber(L, 3, 1.0))));
                 return 1;
             }},
            {"stop_animation", [](lua_State* L) {
                 api::stop_animation(world_of(L), check_entity(L, 1));
                 return 0;
             }},
            {"set_animation_speed", [](lua_State* L) {
                 api::set_animation_speed(world_of(L), check_entity(L, 1), static_cast<float>(luaL_checknumber(L, 2)));
                 return 0;
             }},
            {"animation", [](lua_State* L) {
                 if (const auto a = api::current_animation(world_of(L), check_entity(L, 1))) lua_pushstring(L, a->c_str());
                 else lua_pushnil(L);
                 return 1;
             }},
            {"cross_fade", [](lua_State* L) {
                 lua_pushboolean(L, api::cross_fade(world_of(L), check_entity(L, 1), luaL_checkstring(L, 2), static_cast<float>(luaL_checknumber(L, 3)),
                                                    static_cast<float>(luaL_optnumber(L, 4, 1.0))));
                 return 1;
             }},
            {"set_animation_parameter", [](lua_State* L) {
                 asset::Json v = lua_isboolean(L, 3) ? asset::Json(static_cast<bool>(lua_toboolean(L, 3))) : asset::Json(luaL_checknumber(L, 3));
                 lua_pushboolean(L, api::set_animation_parameter(world_of(L), check_entity(L, 1), luaL_checkstring(L, 2), v));
                 return 1;
             }},
            {"set_animation_trigger", [](lua_State* L) {
                 lua_pushboolean(L, api::set_animation_parameter(world_of(L), check_entity(L, 1), luaL_checkstring(L, 2), asset::Json(true)));
                 return 1;
             }},
            // ---- Effects (needs an emitter component) ----
            {"burst", [](lua_State* L) {
                 lua_pushboolean(L, api::emitter_burst(world_of(L), check_entity(L, 1), static_cast<int>(luaL_checkinteger(L, 2))));
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
            // ---- Time (gameplay foundations, part 2) ----
            {"real_time", [](lua_State* L) {
                 lua_pushnumber(L, world_of(L).real_time());
                 return 1;
             }},
            {"paused", [](lua_State* L) {
                 lua_pushboolean(L, world_of(L).paused());
                 return 1;
             }},
            {"set_paused", [](lua_State* L) {
                 world_of(L).set_paused(lua_toboolean(L, 1) != 0);
                 return 0;
             }},
            {"time_scale", [](lua_State* L) {
                 lua_pushnumber(L, world_of(L).time_scale());
                 return 1;
             }},
            {"set_time_scale", [](lua_State* L) {
                 world_of(L).set_time_scale(luaL_checknumber(L, 1));
                 return 0;
             }},
            // noor.after(seconds, fn [, {real = true}]) / noor.every(seconds, fn [, ...]) -> a timer (t:cancel())
            {"after", [](lua_State* L) { return add_timer(L, false); }},
            {"every", [](lua_State* L) { return add_timer(L, true); }},
            // noor.start(fn): runs fn now as a sequence that may noor.wait
            {"start", [](lua_State* L) {
                 Impl* self = of(L);
                 luaL_checktype(L, 1, LUA_TFUNCTION);
                 if (self->current == Guid{} || !self->instances.contains(self->current))
                     luaL_errorL(L, "noor.start: call it from a script's hook (a sequence belongs to its entity)");
                 lua_State* T = lua_newthread(L);
                 const int ref = lua_ref(L, -1);
                 lua_pop(L, 1);
                 lua_pushvalue(L, 1);
                 lua_xmove(L, T, 1);
                 const std::uint64_t id = self->next_job++;
                 self->sequences[id] = {self->current, T, ref, -1.0, false};
                 self->sequence_of[T] = id;
                 self->resume(id);
                 return 0;
             }},
            // noor.wait(seconds [, {real = true}]): inside noor.start, carries on that much later
            {"wait", [](lua_State* L) {
                 Impl* self = of(L);
                 const auto s = self->sequence_of.find(L);
                 if (s == self->sequence_of.end())
                     luaL_errorL(L, "noor.wait works inside noor.start(function() ... end), not in a hook itself");
                 const double seconds = std::max(0.0, static_cast<double>(luaL_checknumber(L, 1)));
                 const bool real = lua_istable(L, 2) && (lua_getfield(L, 2, "real"), lua_toboolean(L, -1) != 0);
                 auto& seq = self->sequences[s->second];
                 seq.real = real;
                 seq.due = self->clock(real) + seconds;
                 return lua_yield(L, 0);
             }},
            {"find", [](lua_State* L) {
                 if (auto e = api::find(world_of(L), luaL_checkstring(L, 1))) push_entity(L, *e);
                 else lua_pushnil(L);
                 return 1;
             }},
            // noor.find_all(tag) -> the entities with that tag, in scene order (a group)
            {"find_all", [](lua_State* L) {
                 const auto all = api::find_all(world_of(L), luaL_checkstring(L, 1));
                 lua_createtable(L, static_cast<int>(all.size()), 0);
                 for (std::size_t i = 0; i < all.size(); ++i) {
                     push_entity(L, all[i]);
                     lua_rawseti(L, -2, static_cast<int>(i + 1));
                 }
                 return 1;
             }},
            // noor.spawn(prefab, position [, euler [, parent]]) -> the new instance (it starts next frame), or nil
            {"spawn", [](lua_State* L) {
                 const char* prefab = luaL_checkstring(L, 1);
                 const math::Vec3 at = check_vec(L, 2);
                 math::Quat rot{};
                 if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) rot = api::quat_from_euler(check_vec(L, 3));
                 std::optional<Guid> parent;
                 if (lua_gettop(L) >= 4 && !lua_isnil(L, 4)) parent = check_entity(L, 4);
                 if (auto e = api::spawn(world_of(L), prefab, at, rot, parent)) {
                     push_entity(L, *e);
                 } else {
                     of(L)->warn_once(std::format("noor.spawn: no prefab '{}' (an asset id, or a path like Assets/Bullet.noorprefab)", prefab));
                     lua_pushnil(L);
                 }
                 return 1;
             }},
            // noor.play_sound(clip [, position [, volume [, bus]]]) -> true if it started
            {"play_sound", [](lua_State* L) {
                 auto* a = audio_of(L);
                 const char* clip = luaL_checkstring(L, 1);
                 std::optional<math::Vec3> at;
                 if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) at = check_vec(L, 2);
                 const float volume = static_cast<float>(luaL_optnumber(L, 3, 1.0));
                 const char* bus = luaL_optstring(L, 4, "sfx");
                 lua_pushboolean(L, a && a->play_one_shot(clip, at, volume, bus));
                 return 1;
             }},
            // noor.set_volume(bus, volume): master, music, sfx, ui, voice
            {"set_volume", [](lua_State* L) {
                 if (auto* a = audio_of(L)) a->set_bus_volume(luaL_checkstring(L, 1), static_cast<float>(luaL_checknumber(L, 2)));
                 return 0;
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
            // noor.shape_cast(shape, from, to [, ignore]) -> hit table or nil (2D part 2b): a shape swept along a line
            {"shape_cast", [](lua_State* L) {
                 auto* p = physics_of(L);
                 const Json shape = to_json(L, 1);
                 const math::Vec3 from = check_vec(L, 2);
                 const math::Vec3 to = check_vec(L, 3);
                 std::optional<Guid> ignore;
                 if (lua_gettop(L) >= 4 && !lua_isnil(L, 4)) ignore = check_entity(L, 4);
                 const auto hit = p ? p->shape_cast(shape, from, to, ignore) : std::nullopt;
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
            // noor.overlap(shape, at [, ignore]) -> a list of the entities whose bodies overlap the shape there
            {"overlap", [](lua_State* L) {
                 auto* p = physics_of(L);
                 const Json shape = to_json(L, 1);
                 const math::Vec3 at = check_vec(L, 2);
                 std::optional<Guid> ignore;
                 if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) ignore = check_entity(L, 3);
                 const auto found = p ? p->overlap(shape, at, ignore) : std::vector<Guid>{};
                 lua_createtable(L, static_cast<int>(found.size()), 0);
                 for (std::size_t i = 0; i < found.size(); ++i) {
                     push_entity(L, found[i]);
                     lua_rawseti(L, -2, static_cast<int>(i + 1));
                 }
                 return 1;
             }},
            // noor.water_height(x, z) -> the water surface's height there now (waves included), or nil (8D part 2)
            {"water_height", [](lua_State* L) {
                 const auto h = api::water_height(world_of(L), static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2)));
                 if (h) lua_pushnumber(L, *h);
                 else lua_pushnil(L);
                 return 1;
             }},
            // noor.water_normal(x, z) -> the surface's normal there (a vector), or nil
            {"water_normal", [](lua_State* L) {
                 const auto n = api::water_normal(world_of(L), static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2)));
                 if (n) lua_pushvector(L, n->x, n->y, n->z);
                 else lua_pushnil(L);
                 return 1;
             }},
            // noor.water_flow(x, z) -> the water's own velocity there (m/s: a river's current), or nil (8D part 6)
            {"water_flow", [](lua_State* L) {
                 const auto s = world_of(L).water_at(static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2)));
                 if (s) lua_pushvector(L, s->flow.x, s->flow.y, s->flow.z);
                 else lua_pushnil(L);
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
        // noor.ui.text(text, x, y [, {size=24, color="#FFFFFF", anchor="top_left"}])
        // noor.ui.rect(x, y, w, h [, {color="#FFFFFF", anchor="top_left"}])
        static const luaL_Reg ui_functions[] = {
            {"text", [](lua_State* L) {
                 std::string text = luaL_checkstring(L, 1);
                 const math::Vec2 at{static_cast<float>(luaL_checknumber(L, 2)), static_cast<float>(luaL_checknumber(L, 3))};
                 const auto o = ui_options(L, 4);
                 api::ui_text(world_of(L), std::move(text), at, o.size, o.color, o.anchor);
                 return 0;
             }},
            {"rect", [](lua_State* L) {
                 const math::Vec2 at{static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2))};
                 const math::Vec2 size{static_cast<float>(luaL_checknumber(L, 3)), static_cast<float>(luaL_checknumber(L, 4))};
                 const auto o = ui_options(L, 5);
                 api::ui_rect(world_of(L), at, size, o.color, o.anchor);
                 return 0;
             }},
            {nullptr, nullptr},
        };
        lua_newtable(L);
        luaL_register(L, nullptr, noor_functions);
        lua_newtable(L);
        luaL_register(L, nullptr, ui_functions);
        lua_setreadonly(L, -1, true);
        lua_setfield(L, -2, "ui");
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
        // Windows editors (Notepad, PowerShell) may save UTF-8 with a byte-order mark; Luau refuses it, so skip it.
        if (source.starts_with("\xEF\xBB\xBF")) source.erase(0, 3);
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
        // A file required while another loads (or from a hook) runs within the time the caller already has.
        const double outer = deadline;
        if (outer <= 0.0) deadline = lua_clock() + options.call_budget_seconds;
        const int status = lua_pcall(T, 0, 1, 0);
        deadline = outer;
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
        const Guid outer = std::exchange(current, id);
        world->set_drawing_while_paused(it->second.always);
        const int status = lua_pcall(L, 1 + nargs_pushed_after, 0, handler);
        current = outer;
        world->set_drawing_while_paused(false);
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

    void each(const char* hook, double dt, bool only_always = false) {
        const auto ids = order;  // hooks may destroy entities
        for (const Guid& id : ids) {
            const auto it = instances.find(id);
            if (it == instances.end() || (only_always && !it->second.always)) continue;
            lua_pushnumber(L, dt);
            call(id, hook, 1);
        }
        std::erase_if(order, [&](const Guid& id) { return !instances.contains(id); });
    }

    // ---- Timers and sequences (gameplay foundations, part 2) ----
    bool may_run(const Guid& owner) const {
        const auto it = instances.find(owner);
        return it != instances.end() && (!world->paused() || it->second.always);
    }
    double clock(bool real) const { return real ? world->real_time() : world->time(); }
    // Due "now" or earlier: a hair of slack, because thirty steps of 1/60 s add up to 0.49999999999999994, and a 0.5 s
    // timer mustn't wait a frame longer for that.
    bool due(bool real, double when) const { return clock(real) + 1e-9 >= when; }

    // Runs (or resumes) a sequence until it waits, ends or fails.
    void resume(std::uint64_t id) {
        auto it = sequences.find(id);
        if (it == sequences.end()) return;
        Sequence& s = it->second;
        const Guid outer = std::exchange(current, s.owner);
        world->set_drawing_while_paused(instances.contains(s.owner) && instances.at(s.owner).always);
        const double outer_deadline = deadline;
        if (outer_deadline <= 0.0) deadline = lua_clock() + options.call_budget_seconds;
        const int nargs = 0;  // first start or after a wait: nothing passed in
        s.due = -2.0;  // running
        const int status = lua_resume(s.thread, nullptr, nargs);
        deadline = outer_deadline;
        current = outer;
        world->set_drawing_while_paused(false);
        it = sequences.find(id);
        if (it == sequences.end()) return;
        if (status == LUA_YIELD && it->second.due >= 0.0) return;  // noor.wait set when to come back
        if (status != LUA_OK && status != LUA_YIELD) {
            report(std::format("{}\n{} (a noor.start sequence on '{}'); that sequence is stopped", lua_tostring(it->second.thread, -1),
                               lua_debugtrace(it->second.thread), world ? api::name(*world, it->second.owner).value_or("?") : "?"));
        } else if (status == LUA_YIELD) {
            report(std::format("a noor.start sequence on '{}' yielded without noor.wait; it is stopped",
                               world ? api::name(*world, it->second.owner).value_or("?") : "?"));
        }
        drop_sequence(id);
    }
    void drop_sequence(std::uint64_t id) {
        const auto it = sequences.find(id);
        if (it == sequences.end()) return;
        sequence_of.erase(it->second.thread);
        lua_unref(L, it->second.ref);
        sequences.erase(it);
    }
    void drop_jobs_of(const Guid& owner) {
        for (auto it = timers.begin(); it != timers.end();) {
            if (it->second.owner == owner) {
                lua_unref(L, it->second.fn);
                it = timers.erase(it);
            } else {
                ++it;
            }
        }
        std::vector<std::uint64_t> doomed;
        for (const auto& [id, s] : sequences)
            if (s.owner == owner) doomed.push_back(id);
        for (auto id : doomed) drop_sequence(id);
    }
    // Fires the timers that are due and resumes the sequences whose wait is over, oldest first.
    void run_jobs() {
        std::vector<std::uint64_t> ids;
        for (const auto& [id, t] : timers) ids.push_back(id);
        for (const auto id : ids) {
            auto it = timers.find(id);
            if (it == timers.end() || !may_run(it->second.owner) || !due(it->second.real, it->second.due)) continue;
            Timer t = it->second;
            const bool repeat = t.period > 0.0;
            if (repeat) it->second.due = std::max(t.due + t.period, clock(t.real));  // never more than once a frame
            else timers.erase(it);
            lua_pushcfunction(L, traceback_handler, "traceback");
            const int handler = lua_gettop(L);
            lua_getref(L, t.fn);
            if (!repeat) lua_unref(L, t.fn);
            deadline = lua_clock() + options.call_budget_seconds;
            const Guid outer = std::exchange(current, t.owner);
            world->set_drawing_while_paused(instances.contains(t.owner) && instances.at(t.owner).always);
            const int status = lua_pcall(L, 0, 1, handler);
            current = outer;
            world->set_drawing_while_paused(false);
            deadline = 0.0;
            if (status != LUA_OK) {
                report(std::format("{} (a timer on '{}'); the timer is stopped", lua_tostring(L, -1) ? lua_tostring(L, -1) : "error",
                                   api::name(*world, t.owner).value_or("?")));
                if (const auto again = timers.find(id); again != timers.end()) {
                    lua_unref(L, again->second.fn);
                    timers.erase(again);
                }
            } else if (repeat && lua_isboolean(L, -1) && !lua_toboolean(L, -1)) {  // `every` returning false stops it
                if (const auto again = timers.find(id); again != timers.end()) {
                    lua_unref(L, again->second.fn);
                    timers.erase(again);
                }
            }
            lua_settop(L, handler - 1);
        }
        ids.clear();
        for (const auto& [id, s] : sequences) ids.push_back(id);
        for (const auto id : ids) {
            const auto it = sequences.find(id);
            if (it == sequences.end() || it->second.due < 0.0 || !may_run(it->second.owner) || !due(it->second.real, it->second.due)) continue;
            resume(id);
        }
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
    d.instances[id] = {lua_ref(L, -1), file, it->value("pause", std::string()) == "always"};
    lua_pop(L, 1);
    d.order.push_back(id);
    d.call(id, "on_start", 0);
}

void LuaScriptSystem::update(runtime::World& world, double dt) {
    impl_->world = &world;
    impl_->run_jobs();
    // Paused: only scripts marked "pause": "always" run, on the real clock (a pause menu).
    if (world.paused()) impl_->each("on_update", world.real_dt(), true);
    else impl_->each("on_update", dt);
}

void LuaScriptSystem::fixed_update(runtime::World& world, double dt) {
    impl_->world = &world;
    impl_->each("on_fixed_update", dt);
}

void LuaScriptSystem::late_update(runtime::World& world, double dt) {
    auto& d = *impl_;
    d.world = &world;
    // This frame's events (physics, sprite animations that ended), then the late update.
    for (const auto& e : world.events()) {
        if (!d.instances.contains(e.entity)) continue;
        using K = runtime::Event::Kind;
        if (e.kind == K::AnimationEnd || e.kind == K::AnimationEvent || e.kind == K::Water) {
            lua_pushstring(d.L, e.name.c_str());
            d.call(e.entity, e.kind == K::AnimationEnd ? "on_animation_end" : e.kind == K::Water ? "on_water" : "on_animation_event", 1);
            continue;
        }
        const char* hook = e.kind == K::CollisionEnter ? "on_collision_enter"
                         : e.kind == K::CollisionExit  ? "on_collision_exit"
                         : e.kind == K::TriggerEnter   ? "on_trigger_enter"
                                                       : "on_trigger_exit";
        Impl::push_entity(d.L, e.other);
        d.call(e.entity, hook, 1);
    }
    if (world.paused()) d.each("on_late_update", world.real_dt(), true);
    else d.each("on_late_update", dt);
}

void LuaScriptSystem::entity_destroyed(runtime::World& world, const scene::Entity& entity) {
    auto& d = *impl_;
    d.world = &world;
    if (!d.instances.contains(entity.id)) return;
    d.call(entity.id, "on_destroy", 0);
    d.drop_jobs_of(entity.id);
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
