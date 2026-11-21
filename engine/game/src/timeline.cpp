#include "noor/game/timeline.h"

#include "noor/foundation/log.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/tween.h"

#include <cmath>
#include <unordered_set>

namespace noor::game {

using asset::Json;
namespace api = runtime::api;

std::optional<Guid> timeline_target(runtime::World& world, const Guid& player, std::string_view path) {
    Guid at = player;
    while (!path.empty()) {
        const auto slash = path.find('/');
        const std::string_view part = path.substr(0, slash);
        if (!part.empty()) {
            const auto c = api::child(world, at, part);
            if (!c) return std::nullopt;
            at = *c;
        }
        path = slash == std::string_view::npos ? std::string_view{} : path.substr(slash + 1);
    }
    return world.find(at) ? std::optional(at) : std::nullopt;
}

void apply_timeline(runtime::World& world, const Guid& player, const asset::Timeline& timeline, double t) {
    for (const auto& track : timeline.tracks) {
        if (track.kind != asset::TimelineTrack::Kind::Value) continue;
        const auto span = asset::key_span(track, t);
        const auto target = span ? timeline_target(world, player, track.target) : std::nullopt;
        if (!target) continue;
        const double k = span->a == span->b ? 0.0 : runtime::ease(span->b->ease.empty() ? "linear" : span->b->ease, span->f).value_or(span->f);
        runtime::write_property(world, *target, track.property, span->a->value, span->b->value, k);
    }
}

const asset::Timeline* TimelineSystem::timeline(const Guid& id) {
    auto it = cache_.find(id);
    if (it == cache_.end()) {
        std::optional<asset::Timeline> loaded;
        if (const auto file = resolve_ ? resolve_(id) : std::nullopt) {
            if (auto t = asset::load_timeline_file(*file)) loaded = std::move(*t);
            else NOOR_LOG_WARN("Timeline", "{}", t.error().describe());
        } else {
            NOOR_LOG_WARN("Timeline", "no timeline asset {}", id.to_string());
        }
        it = cache_.emplace(id, std::move(loaded)).first;
    }
    return it->second ? &*it->second : nullptr;
}

void TimelineSystem::entity_started(runtime::World& world, scene::Entity& entity) {
    auto it = entity.components.find("animation_player");
    if (it == entity.components.end() || !it->is_object()) return;
    Json& c = *it;
    if (c.contains("autoplay") && c["autoplay"].is_string() && !c.contains("playing")) {
        c["playing"] = c["autoplay"];
        c["time"] = nullptr;  // starting: keys at 0 fire on the first step
    }
    (void)world;
}

// Fires call and emit keys in (from, to].
void TimelineSystem::fire(runtime::World& world, const Guid& player, const asset::Timeline& t, double from, double to) {
    for (const auto& track : t.tracks) {
        if (track.kind == asset::TimelineTrack::Kind::Value) continue;
        for (const auto& k : track.keys) {
            if (!(k.t > from && k.t <= to)) continue;
            const auto target = timeline_target(world, player, track.target);
            if (!target) continue;
            if (track.kind == asset::TimelineTrack::Kind::Emit) {
                world.emit(*target, k.message, k.data);
            } else if (!world.invoke(*target, k.function, k.args)) {
                static std::unordered_set<std::string> said;
                const std::string what = std::format("timeline '{}': '{}' has no script function '{}'", t.name,
                                                     api::name(world, *target).value_or("?"), k.function);
                if (said.insert(what).second) NOOR_LOG_WARN("Timeline", "{}", what);
            }
        }
    }
}

void TimelineSystem::update(runtime::World& world, double dt) {
    // Which entities play something now (a script may start one on any entity, so all players are looked at).
    std::vector<Guid> players;
    for (const auto& e : world.scene().entities)
        if (e.has("animation_player") && e.components.at("animation_player").contains("playing") &&
            e.components.at("animation_player")["playing"].is_string())
            players.push_back(e.id);
    for (const Guid& id : players) {
        const Json* comp = api::component(world, id, "animation_player");
        if (!comp) continue;
        Json c = *comp;
        const std::string name = c["playing"].get<std::string>();
        const Json& list = c.value("timelines", Json::object());
        const auto asset_id = list.contains(name) && list[name].is_string() ? Guid::parse(list[name].get<std::string>()) : Result<Guid>(std::unexpected(Error{}));
        const asset::Timeline* t = asset_id ? timeline(*asset_id) : nullptr;
        if (!t) {
            c.erase("playing");
            api::set_component(world, id, "animation_player", std::move(c));
            continue;
        }
        const double speed = c.contains("speed") && c["speed"].is_number() ? std::max(0.0, c["speed"].get<double>()) : 1.0;
        const bool starting = !c.contains("time") || !c["time"].is_number();
        const double before = starting ? -1e-9 : c["time"].get<double>();
        double now = std::max(0.0, before) + dt * speed;
        bool ended = false;
        if (t->loop && t->length > 0.0) {
            if (now > t->length) {  // round the end: what's left of this pass, then the next from its start
                fire(world, id, *t, before, t->length);
                now = std::fmod(now, t->length);
                fire(world, id, *t, -1e-9, now);
            } else {
                fire(world, id, *t, before, now);
            }
        } else {
            if (now + 1e-9 >= t->length) {
                now = t->length;
                ended = true;
            }
            fire(world, id, *t, before, now);
        }
        apply_timeline(world, id, *t, now);
        // Its state, unless something it called changed it (a key that starts another timeline).
        const Json* after = api::component(world, id, "animation_player");
        if (!after || after->value("playing", Json()) != c["playing"] || after->value("time", Json()) != c.value("time", Json())) continue;
        Json updated = *after;
        if (ended) {
            updated.erase("playing");
            updated["time"] = now;
            api::set_component(world, id, "animation_player", std::move(updated));
            world.emit(id, "timeline_end", {{"name", name}});
        } else {
            updated["time"] = now;
            api::set_component(world, id, "animation_player", std::move(updated));
        }
    }
}

} // namespace noor::game
