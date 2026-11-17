#pragma once
// Undo/redo for scene editing (spec 6.4 "undo across modules", 26.4 save/undo invariants).
//
// Every change to a scene goes through a Command that can apply and revert itself. History keeps
// the stacks, groups several commands into one undo step (transactions, e.g. "drag 5 objects"),
// and knows whether the scene changed since the last save.
//
// Invariant (tested): after any sequence of commands, undoing everything restores a scene whose
// saved text is byte-identical to the original; redoing everything restores the final text.

#include "noor/scene/scene.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace noor::scene {

class Command {
public:
    virtual ~Command() = default;
    [[nodiscard]] virtual std::string label() const = 0;
    // Returns false if the command cannot apply (e.g. its entity no longer exists); nothing changes then.
    virtual bool apply(Scene& scene) = 0;
    virtual void revert(Scene& scene) = 0;
};

class History {
public:
    explicit History(std::size_t max_steps = 1000) : max_steps_(max_steps) {}

    // Applies the command and records it. Clears the redo stack. Returns false if it didn't apply.
    bool execute(Scene& scene, std::unique_ptr<Command> command);

    // Groups the commands executed until end_transaction() into one undo step. Nested calls fold
    // into the outermost transaction. An empty transaction leaves no step behind.
    void begin_transaction(std::string label);
    void end_transaction();

    bool undo(Scene& scene);
    bool redo(Scene& scene);

    [[nodiscard]] bool can_undo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !redo_.empty(); }
    [[nodiscard]] std::optional<std::string> undo_label() const;
    [[nodiscard]] std::optional<std::string> redo_label() const;
    [[nodiscard]] std::size_t undo_count() const noexcept { return undo_.size(); }

    // Save tracking: modified() is true when the scene differs from the last mark_saved() state
    // (undoing back to the saved state counts as unmodified).
    void mark_saved() noexcept { saved_marker_ = current_marker(); }
    [[nodiscard]] bool modified() const noexcept { return saved_marker_ != current_marker(); }

    void clear();

private:
    struct Step {
        std::string label;
        std::vector<std::unique_ptr<Command>> commands;
        std::uint64_t id = 0;
    };
    [[nodiscard]] std::uint64_t current_marker() const noexcept { return undo_.empty() ? 0 : undo_.back().id; }

    std::vector<Step> undo_;
    std::vector<Step> redo_;
    std::optional<Step> open_;  // transaction being built
    int depth_ = 0;
    std::uint64_t next_id_ = 1;
    std::uint64_t saved_marker_ = 0;
    std::size_t max_steps_;
};

// ---- Built-in commands --------------------------------------------------------------------

std::unique_ptr<Command> set_transform(const Guid& entity, const Transform& value);
std::unique_ptr<Command> rename(const Guid& entity, std::string name);
// Sets a component's JSON; a null value removes the component.
std::unique_ptr<Command> set_component(const Guid& entity, std::string type, asset::Json value);
// Adds an entity at the end (or right after its parent's subtree, keeping parents first).
std::unique_ptr<Command> add_entity(Entity entity);
// Removes an entity and all of its descendants; undo restores them at their original positions.
std::unique_ptr<Command> remove_entity(const Guid& entity);
// Changes an entity's parent (nullopt = root), keeping its local transform. Refuses cycles.
std::unique_ptr<Command> reparent(const Guid& entity, std::optional<Guid> new_parent);
// Moves an entity (with its subtree) among its siblings: before `sibling`, or after the last one
// (nullopt). Sibling order matters where order means something (CSG brushes: the operation
// history). Refuses a sibling with another parent; a move that changes nothing isn't applied.
std::unique_ptr<Command> move_before(const Guid& entity, std::optional<Guid> sibling);

} // namespace noor::scene
