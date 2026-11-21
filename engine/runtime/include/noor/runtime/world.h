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

#include <algorithm>
#include <functional>

namespace noor::scene {
struct Prefab;
}

#include <cstdint>
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <optional>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace noor::runtime {

class World;

// Something that happened this frame, for scripts (collisions and triggers from physics, a sprite animation that
// ended, ...). Events are cleared at the end of each frame, after late_update.
struct Event {
    enum class Kind : std::uint8_t { CollisionEnter, CollisionExit, TriggerEnter, TriggerExit, AnimationEnd, AnimationEvent, Water };
    Kind kind;
    Guid entity;       // the entity the event is for
    Guid other;        // the other entity involved (AnimationEnd, AnimationEvent, Water: the entity itself)
    std::string name;  // AnimationEnd: the animation; AnimationEvent (8C part 4): the event's name; Water (8D part 5): the
                       // character's new state in the water ("dry", "wading", "swimming", "under")
};

// A HUD element a script draws this frame (noor.ui.* in Lua, Ui.* in C#). Positions and sizes are
// in UI units: the screen is kUiHeight (720) units tall and as many wide as its shape gives, so a
// HUD looks the same at any window size. `anchor` is both the point of the screen the position is
// measured from and the point of the element that goes there (Center: centred on screen centre +
// position; BottomRight with position (-20, -20): 20 units in from the corner). +x right, +y down.
enum class UiAnchor : std::uint8_t { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };
inline constexpr float kUiHeight = 720.0f;
struct UiCommand {
    enum class Kind : std::uint8_t { Rect, Text } kind = Kind::Rect;
    UiAnchor anchor = UiAnchor::TopLeft;
    math::Vec2 position{0, 0};
    math::Vec2 size{0, 0};          // Rect
    float text_size = 24.0f;        // Text: line height
    math::Vec4 color{1, 1, 1, 1};   // linear RGB, straight alpha
    std::string text;               // UTF-8; '\n' starts a new line
    bool runs_while_paused = false; // drawn by a script that runs while paused (gameplay foundations, part 2)
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
    // Added to a world (World::add_system): a system offering commands to scripts registers them here.
    virtual void attached(World&) {}
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

    // ---- Pause and time scale (gameplay foundations, part 2) ----
    // Paused, the game's clock stops: systems' update and late_update get dt 0, no fixed steps run, so physics,
    // animations, particles, cameras and game-time timers freeze. Scripts marked "pause": "always" keep running (their
    // hooks get the real dt): pause menus. The time scale stretches the game's clock (0.25: slow motion; physics keeps
    // its fixed step size, taking fewer of them, so it stays deterministic). Changed by a script during update, they
    // apply to the rest of that frame at once (physics, late update, the clock).
    void set_paused(bool paused) noexcept { paused_ = paused; }
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    void set_time_scale(double scale) noexcept { time_scale_ = std::clamp(scale, 0.0, 10.0); }
    [[nodiscard]] double time_scale() const noexcept { return time_scale_; }
    // Seconds that really passed since play started, paused or not, and this frame's real step.
    [[nodiscard]] double real_time() const noexcept { return real_time_; }
    [[nodiscard]] double real_dt() const noexcept { return real_dt_; }
    [[nodiscard]] std::uint64_t frame_count() const noexcept { return frames_; }
    [[nodiscard]] std::uint64_t fixed_step_count() const noexcept { return fixed_steps_; }
    // How far between the last and next fixed step (0..1), for interpolating fixed-step state.
    [[nodiscard]] double fixed_alpha() const noexcept { return accumulator_ / fixed_dt; }

    // ---- HUD ----
    // What scripts drew on the HUD this frame, in order (later on top). Cleared at frame start, so
    // scripts draw every frame they want something shown (immediate mode).
    void ui(UiCommand command) {
        command.runs_while_paused = drawing_while_paused_;
        if (ui_.size() < kMaxUiCommands) ui_.push_back(std::move(command));
    }
    // Script systems say whether the script drawing now runs while paused. While the game is paused, what the paused
    // scripts drew last stays on screen (the score under a pause menu) and the running ones draw over it.
    void set_drawing_while_paused(bool on) noexcept { drawing_while_paused_ = on; }
    [[nodiscard]] const std::vector<UiCommand>& ui_commands() const noexcept { return ui_; }
    static constexpr std::size_t kMaxUiCommands = 20000;  // a runaway loop can't eat the memory

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

    // ---- Commands ----
    // Services systems offer to scripts by name ("emitter.burst", ...), so the script API (which knows the World,
    // not the systems) can reach them. A system registers its commands when attached; a command for a system that
    // isn't there does nothing and returns false.
    using CommandHandler = std::function<void(World&, const Guid& entity, const asset::Json& args)>;
    void handle(std::string name, CommandHandler handler) { commands_[std::move(name)] = std::move(handler); }
    bool command(std::string_view name, const Guid& entity, const asset::Json& args);

    // ---- Water (Milestone 8D, part 2) ----
    // The water surface at world (x, z) now, from whoever keeps the water (game::WaterSystem); nothing without one or
    // where there's no water.
    struct WaterSample {
        float height = 0.0f;  // world y
        math::Vec3 normal{0, 1, 0};
        Guid entity;          // the water body's entity
        math::Vec3 flow{0, 0, 0};  // the water's own velocity there (m/s; a river's current, 8D part 6)
    };
    using WaterQuery = std::function<std::optional<WaterSample>(const World&, float x, float z)>;
    void set_water_query(WaterQuery query) { water_query_ = std::move(query); }
    [[nodiscard]] std::optional<WaterSample> water_at(float x, float z) const {
        return water_query_ ? water_query_(*this, x, z) : std::nullopt;
    }

    // ---- Spawning prefabs (gameplay foundations, part 1) ----
    // The host (game::make_world) says how to find prefabs: by asset id, and an asset path ("Assets/Bullet.noorprefab")
    // to its id. Without them nothing can be spawned.
    using PrefabLookup = std::function<const scene::Prefab*(const Guid&)>;
    using AssetIdLookup = std::function<std::optional<Guid>(std::string_view path)>;
    void set_prefabs(PrefabLookup prefabs, AssetIdLookup asset_ids) {
        prefabs_ = std::move(prefabs);
        asset_ids_ = std::move(asset_ids);
    }
    [[nodiscard]] const scene::Prefab* prefab(const Guid& id) const { return prefabs_ ? prefabs_(id) : nullptr; }
    [[nodiscard]] std::optional<Guid> asset_id(std::string_view path) const { return asset_ids_ ? asset_ids_(path) : std::nullopt; }
    // The id the next spawned instance gets: the same in every run of the same game (the scene's id and a count),
    // so built games stay identical to the bit (a random id would change the order systems meet entities in).
    [[nodiscard]] Guid next_spawn_id();
    // Who moves bodies and characters at once (the physics system, when attached): true if it handled the entity.
    using Teleporter = std::function<bool(World&, const Guid&, math::Vec3, std::optional<math::Quat>)>;
    void set_teleporter(Teleporter t) { teleporter_ = std::move(t); }
    [[nodiscard]] const Teleporter& teleporter() const noexcept { return teleporter_; }

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
    std::map<std::string, CommandHandler, std::less<>> commands_;
    std::optional<bool> mouse_capture_request_;
    PrefabLookup prefabs_;
    AssetIdLookup asset_ids_;
    Teleporter teleporter_;
    std::uint64_t spawned_ = 0;
    WaterQuery water_query_;
    std::unordered_map<Guid, math::Mat4> render_poses_;
    std::vector<UiCommand> ui_;
    double time_ = 0.0;
    bool paused_ = false;
    bool drawing_while_paused_ = false;
    std::vector<UiCommand> frozen_ui_;  // the paused scripts' HUD, kept while paused
    double time_scale_ = 1.0;
    double real_time_ = 0.0, real_dt_ = 0.0;
    double accumulator_ = 0.0;
    std::uint64_t frames_ = 0;
    std::uint64_t fixed_steps_ = 0;
};

} // namespace noor::runtime
