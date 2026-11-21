#include "noor/runtime/world.h"

#include "noor/foundation/log.h"

#include <algorithm>
#include <format>

namespace noor::runtime {

World::World(scene::Scene scene, InputMap input_map) : scene_(std::move(scene)), input_(std::move(input_map)) { reindex(); }

World::~World() {
    for (auto& s : systems_) s->shutdown(*this);
}

void World::add_system(std::unique_ptr<System> system) {
    systems_.push_back(std::move(system));
    systems_.back()->attached(*this);
}

bool World::command(std::string_view name, const Guid& entity, const asset::Json& args) {
    const auto it = commands_.find(name);
    if (it == commands_.end()) return false;
    it->second(*this, entity, args);
    return true;
}

void World::reindex() {
    index_.clear();
    for (std::size_t i = 0; i < scene_.entities.size(); ++i) index_.emplace(scene_.entities[i].id, i);
}

scene::Entity* World::find(const Guid& id) {
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : &scene_.entities[it->second];
}

scene::Entity& World::spawn(scene::Entity entity) {
    scene_.entities.push_back(std::move(entity));
    index_[scene_.entities.back().id] = scene_.entities.size() - 1;
    return scene_.entities.back();
}

void World::destroy(const Guid& id) { destroy_queue_.push_back(id); }

std::uint64_t World::listen(std::optional<Guid> emitter, std::string name, const Guid& owner, System* system, std::uint64_t handle) {
    const std::uint64_t id = next_listener_++;
    ++listened_names_[name];
    listeners_[id] = {emitter, std::move(name), owner, system, handle};
    return id;
}

void World::unlisten(std::uint64_t id) {
    const auto it = listeners_.find(id);
    if (it == listeners_.end()) return;
    if (--listened_names_[it->second.name] <= 0) listened_names_.erase(it->second.name);
    listeners_.erase(it);
}

int World::emit(std::optional<Guid> emitter, std::string_view name, const asset::Json& data) {
    if (!listened_names_.contains(std::string(name))) return 0;
    if (emit_depth_ >= 32) {
        message_errors_.push_back(std::format("the message '{}' was sent from its own listeners 32 deep (a loop?): stopped", name));
        NOOR_LOG_ERROR("Script", "{}", message_errors_.back());
        return 0;
    }
    // Who hears it, decided now: listeners added while it's delivered wait for the next one, ones removed don't hear it.
    std::vector<std::uint64_t> ids;
    for (const auto& [id, l] : listeners_)
        if (l.name == name && l.emitter == emitter) ids.push_back(id);
    ++emit_depth_;
    int heard = 0;
    for (const auto id : ids) {
        const auto it = listeners_.find(id);
        if (it == listeners_.end()) continue;
        const Listener l = it->second;
        l.system->deliver(*this, l.handle, emitter, name, data);
        ++heard;
    }
    --emit_depth_;
    return heard;
}

std::optional<asset::Json> World::invoke(const Guid& entity, std::string_view function, const asset::Json& args) {
    asset::Json result;
    for (auto& s : systems_)
        if (s->invoke(*this, entity, function, args, result)) return result;
    return std::nullopt;
}

// Engine events (collisions, triggers, animations ending, water) are messages too, from the entity they happened to,
// so another entity's script can listen: a door listening to a pressure plate's "trigger_enter".
void World::emit_events_as_messages() {
    if (listeners_.empty()) return;
    static constexpr const char* kNames[] = {"collision_enter", "collision_exit", "trigger_enter", "trigger_exit",
                                             "animation_end", "animation_event", "water"};
    for (const Event& e : std::vector<Event>(events_)) {
        const char* name = kNames[static_cast<int>(e.kind)];
        if (!has_listeners(name)) continue;
        asset::Json data = {{"other", e.other.to_string()}};
        if (!e.name.empty()) data["name"] = e.name;
        emit(e.entity, name, data);
    }
}

Guid World::ui_emitter(std::uint32_t document) const {
    Guid tag;  // "ui" and the number: never an entity's id (those are random, or derived from a count without this)
    tag.bytes[0] = 'u';
    tag.bytes[1] = 'i';
    for (int i = 0; i < 4; ++i) tag.bytes[static_cast<std::size_t>(15 - i)] = static_cast<std::uint8_t>(document >> (8 * i));
    return Guid::derive(scene_.id, tag);
}

Guid World::next_spawn_id() {
    Guid count;
    std::uint64_t n = ++spawned_;
    for (int i = 0; i < 8; ++i) count.bytes[static_cast<std::size_t>(15 - i)] = static_cast<std::uint8_t>(n >> (8 * i));
    return Guid::derive(scene_.id, count);
}

void World::start_new_entities() {
    // Entities may be spawned while starting others: iterate by index, re-reading the size.
    for (std::size_t i = 0; i < scene_.entities.size(); ++i) {
        const Guid id = scene_.entities[i].id;
        if (!started_.insert(id).second) continue;
        for (const bool late : {false, true}) {
            for (auto& s : systems_) {
                if (s->starts_late() != late) continue;
                scene::Entity* e = find(id);  // a system may have spawned entities (reallocation)
                if (!e) break;
                s->entity_started(*this, *e);
            }
        }
    }
}

void World::flush_destroyed() {
    if (destroy_queue_.empty()) return;
    // Expand to whole subtrees.
    std::unordered_set<Guid> doomed(destroy_queue_.begin(), destroy_queue_.end());
    destroy_queue_.clear();
    for (bool grew = true; grew;) {
        grew = false;
        for (const auto& e : scene_.entities) {
            if (e.parent && doomed.contains(*e.parent) && doomed.insert(e.id).second) grew = true;
        }
    }
    for (const auto& e : scene_.entities) {
        if (!doomed.contains(e.id)) continue;
        for (auto& s : systems_) s->entity_destroyed(*this, e);
    }
    // Listeners of or on a destroyed entity go with it.
    std::vector<std::uint64_t> gone;
    for (const auto& [id, l] : listeners_)
        if (doomed.contains(l.owner) || (l.emitter && doomed.contains(*l.emitter))) gone.push_back(id);
    for (const auto id : gone) unlisten(id);
    // UI documents of a destroyed entity close with it.
    for (auto it = ui_owners_.begin(); it != ui_owners_.end();) {
        if (doomed.contains(it->second)) {
            if (ui_documents_) ui_documents_->close(it->first);
            it = ui_owners_.erase(it);
        } else {
            ++it;
        }
    }
    std::erase_if(scene_.entities, [&](const scene::Entity& e) { return doomed.contains(e.id); });
    for (const Guid& id : doomed) started_.erase(id);
    reindex();
}

void World::frame(double dt, const InputState& input) {
    real_dt_ = std::clamp(dt, 0.0, max_dt);
    dt = paused_ ? 0.0 : real_dt_ * time_scale_;  // the game's clock (gameplay foundations, part 2)
    render_poses_.clear();
    // Paused: the HUD the paused scripts drew last frame stays (they don't run to draw it again); the scripts that run
    // while paused draw over it afresh.
    if (paused_) {
        if (frozen_ui_.empty())
            for (const auto& c : ui_)
                if (!c.runs_while_paused) frozen_ui_.push_back(c);
        ui_ = frozen_ui_;
    } else {
        frozen_ui_.clear();
        ui_.clear();
    }
    input_.begin_frame(input);
    start_new_entities();
    // Messages queued since last frame (UI events): before anything runs, so scripts see them this frame.
    for (auto& q : std::exchange(queued_, {})) emit(q.emitter, q.name, q.data);
    // Gamepads plugged in or out (part 6e): game-wide messages {player, name}.
    for (const int p : input_.pads_connected())
        emit(std::nullopt, "pad_connected", {{"player", p}, {"name", input_.state().pads[static_cast<std::size_t>(p - 1)].name}});
    for (const int p : input_.pads_disconnected()) emit(std::nullopt, "pad_disconnected", {{"player", p}});
    for (auto& s : systems_) s->update(*this, dt);
    // A script paused, unpaused or changed the time scale just now: the rest of the frame (physics, late update, the
    // clock) already goes by it, so a pause freezes the world at once, not a physics step later.
    dt = paused_ ? 0.0 : real_dt_ * time_scale_;
    tweens_.step(*this, dt, real_dt_);

    accumulator_ += dt;
    int steps = 0;
    while (accumulator_ >= fixed_dt && steps < max_fixed_steps) {
        for (auto& s : systems_) s->fixed_update(*this, fixed_dt);
        accumulator_ -= fixed_dt;
        ++steps;
        ++fixed_steps_;
    }
    if (steps == max_fixed_steps && accumulator_ >= fixed_dt) accumulator_ = 0.0;  // too slow: drop, don't spiral

    emit_events_as_messages();
    for (auto& s : systems_) s->late_update(*this, dt);
    events_.clear();
    flush_destroyed();
    time_ += dt;
    real_time_ += real_dt_;
    ++frames_;
}

} // namespace noor::runtime
