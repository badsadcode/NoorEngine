#include "noor/runtime/world.h"

#include <algorithm>

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
    std::erase_if(scene_.entities, [&](const scene::Entity& e) { return doomed.contains(e.id); });
    for (const Guid& id : doomed) started_.erase(id);
    reindex();
}

void World::frame(double dt, const InputState& input) {
    dt = std::clamp(dt, 0.0, max_dt);
    render_poses_.clear();
    ui_.clear();
    input_.begin_frame(input);
    start_new_entities();
    for (auto& s : systems_) s->update(*this, dt);

    accumulator_ += dt;
    int steps = 0;
    while (accumulator_ >= fixed_dt && steps < max_fixed_steps) {
        for (auto& s : systems_) s->fixed_update(*this, fixed_dt);
        accumulator_ -= fixed_dt;
        ++steps;
        ++fixed_steps_;
    }
    if (steps == max_fixed_steps && accumulator_ >= fixed_dt) accumulator_ = 0.0;  // too slow: drop, don't spiral

    for (auto& s : systems_) s->late_update(*this, dt);
    events_.clear();
    flush_destroyed();
    time_ += dt;
    ++frames_;
}

} // namespace noor::runtime
