#pragma once
// A running scene (ADR-027). Hosts call frame(dt, input) once per frame; the World runs its systems
// in a fixed order:
//
//   input -> start (entities new since last frame) -> update(dt)
//         -> fixed_update(fixed_dt) x N (60 Hz accumulator, N <= max_fixed_steps)
//         -> late_update(dt) -> destroy queued entities
//
// Systems (behaviours, scripts, physics, audio...) plug in without the World knowing them.

#include "noor/runtime/input.h"
#include "noor/scene/scene.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace noor::runtime {

class World;

// Something that happened this frame, for scripts (collisions and triggers from physics, ...).
// Events are cleared at the end of each frame, after late_update.
struct Event {
    enum class Kind : std::uint8_t { CollisionEnter, CollisionExit, TriggerEnter, TriggerExit };
    Kind kind;
    Guid entity;  // the entity the event is for
    Guid other;   // the other entity involved
};

class System {
public:
    virtual ~System() = default;
    [[nodiscard]] virtual std::string_view name() const = 0;
    // An entity becomes active (scene start, or spawned last frame). Called once per entity.
    virtual void entity_started(World&, scene::Entity&) {}
    // Script systems start entities after the engine's own systems (physics bodies, sounds...)
    // have prepared them, so a script's on_start can use them.
    [[nodiscard]] virtual bool starts_late() const { return false; }
    virtual void update(World&, double /*dt*/) {}
    virtual void fixed_update(World&, double /*fixed_dt*/) {}
    virtual void late_update(World&, double /*dt*/) {}
    // Called before the entity is removed (for each entity of a destroyed subtree).
    virtual void entity_destroyed(World&, const scene::Entity&) {}
    // The World is shutting down (Stop).
    virtual void shutdown(World&) {}
};

class World {
public:
    explicit World(scene::Scene scene, InputMap input_map = InputMap::defaults());
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    void add_system(std::unique_ptr<System> system);
    template <class T>
    [[nodiscard]] T* system() const {
        for (const auto& s : systems_)
            if (auto* t = dynamic_cast<T*>(s.get())) return t;
        return nullptr;
    }

    // One frame. dt is capped at max_dt (a stalled frame doesn't explode the simulation).
    void frame(double dt, const InputState& input);

    // ---- State ----
    [[nodiscard]] scene::Scene& scene() noexcept { return scene_; }
    [[nodiscard]] const scene::Scene& scene() const noexcept { return scene_; }
    [[nodiscard]] const Input& input() const noexcept { return input_; }
    [[nodiscard]] double time() const noexcept { return time_; }           // seconds of game time
    [[nodiscard]] std::uint64_t frame_count() const noexcept { return frames_; }
    [[nodiscard]] std::uint64_t fixed_step_count() const noexcept { return fixed_steps_; }
    // How far between the last and next fixed step (0..1), for interpolating fixed-step state.
    [[nodiscard]] double fixed_alpha() const noexcept { return accumulator_ / fixed_dt; }

    // ---- Drawing between fixed steps ----
    // Systems that move entities in fixed steps (physics) say where to *draw* them this frame:
    // blended between their last two steps by fixed_alpha(), so motion is smooth whatever the frame
    // rate. Only drawing uses these (World matrices); the simulation and scripts keep the real
    // transforms. Cleared at the start of every frame.
    void set_render_pose(const Guid& id, const math::Mat4& world_matrix) { render_poses_[id] = world_matrix; }
    [[nodiscard]] const std::unordered_map<Guid, math::Mat4>& render_poses() const noexcept { return render_poses_; }

    double fixed_dt = 1.0 / 60.0;
    int max_fixed_steps = 5;
    double max_dt = 0.1;

    // ---- Mouse capture (FPS look) ----
    // Scripts ask; the host decides (it captures on the next frame, and always releases on Escape,
    // focus loss and Stop). input().state().mouse_captured tells what actually happened.
    void request_mouse_capture(bool capture) { mouse_capture_request_ = capture; }
    [[nodiscard]] std::optional<bool> take_mouse_capture_request() { return std::exchange(mouse_capture_request_, std::nullopt); }

    // ---- Events ----
    void post(const Event& event) { events_.push_back(event); }
    [[nodiscard]] const std::vector<Event>& events() const noexcept { return events_; }

    // ---- Entities ----
    [[nodiscard]] scene::Entity* find(const Guid& id);
    // Adds an entity; systems see it (entity_started) at the start of the next frame.
    scene::Entity& spawn(scene::Entity entity);
    // Removes the entity and its descendants at the end of this frame.
    void destroy(const Guid& id);

private:
    void start_new_entities();
    void flush_destroyed();
    void reindex();

    scene::Scene scene_;
    Input input_;
    std::vector<std::unique_ptr<System>> systems_;
    std::unordered_map<Guid, std::size_t> index_;
    std::unordered_set<Guid> started_;
    std::vector<Guid> destroy_queue_;
    std::vector<Event> events_;
    std::optional<bool> mouse_capture_request_;
    std::unordered_map<Guid, math::Mat4> render_poses_;
    double time_ = 0.0;
    double accumulator_ = 0.0;
    std::uint64_t frames_ = 0;
    std::uint64_t fixed_steps_ = 0;
};

} // namespace noor::runtime
