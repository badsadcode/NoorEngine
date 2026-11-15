#include "noor/scene/history.h"

#include <algorithm>
#include <unordered_set>

namespace noor::scene {
namespace {

std::ptrdiff_t index_of(const Scene& s, const Guid& id) {
    for (std::size_t i = 0; i < s.entities.size(); ++i) {
        if (s.entities[i].id == id) return static_cast<std::ptrdiff_t>(i);
    }
    return -1;
}

// Indices of `root` and all its descendants, in scene order.
std::vector<std::size_t> subtree(const Scene& s, const Guid& root) {
    std::unordered_set<Guid> members{root};
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < s.entities.size(); ++i) {
        const Entity& e = s.entities[i];
        if (e.id == root || (e.parent && members.contains(*e.parent))) {
            members.insert(e.id);
            out.push_back(i);
        }
    }
    return out;
}

class SetTransform final : public Command {
public:
    SetTransform(Guid id, Transform value) : id_(id), new_(value) {}
    std::string label() const override { return "Move"; }
    bool apply(Scene& s) override {
        const auto i = index_of(s, id_);
        if (i < 0) return false;
        old_ = s.entities[static_cast<std::size_t>(i)].transform;
        s.entities[static_cast<std::size_t>(i)].transform = new_;
        return true;
    }
    void revert(Scene& s) override {
        if (const auto i = index_of(s, id_); i >= 0) s.entities[static_cast<std::size_t>(i)].transform = old_;
    }

private:
    Guid id_;
    Transform new_, old_;
};

class Rename final : public Command {
public:
    Rename(Guid id, std::string name) : id_(id), new_(std::move(name)) {}
    std::string label() const override { return "Rename"; }
    bool apply(Scene& s) override {
        const auto i = index_of(s, id_);
        if (i < 0) return false;
        old_ = std::exchange(s.entities[static_cast<std::size_t>(i)].name, new_);
        return true;
    }
    void revert(Scene& s) override {
        if (const auto i = index_of(s, id_); i >= 0) s.entities[static_cast<std::size_t>(i)].name = old_;
    }

private:
    Guid id_;
    std::string new_, old_;
};

class SetComponent final : public Command {
public:
    SetComponent(Guid id, std::string type, asset::Json value) : id_(id), type_(std::move(type)), new_(std::move(value)) {}
    std::string label() const override { return new_.is_null() ? "Remove " + type_ : "Edit " + type_; }
    bool apply(Scene& s) override {
        const auto i = index_of(s, id_);
        if (i < 0) return false;
        auto& comps = s.entities[static_cast<std::size_t>(i)].components;
        had_ = comps.contains(type_);
        if (had_) {
            old_ = comps[type_];
            // Remember the key's position so undo restores the exact same file.
            position_ = static_cast<std::size_t>(std::distance(comps.begin(), comps.find(type_)));
        }
        if (new_.is_null()) {
            comps.erase(type_);
        } else {
            comps[type_] = new_;
        }
        return true;
    }
    void revert(Scene& s) override {
        const auto i = index_of(s, id_);
        if (i < 0) return;
        auto& comps = s.entities[static_cast<std::size_t>(i)].components;
        if (!had_) {
            comps.erase(type_);
            return;
        }
        if (comps.contains(type_)) {
            comps[type_] = old_;
            return;
        }
        // Re-insert at its original position (ordered_json keeps insertion order).
        asset::Json rebuilt = asset::Json::object();
        std::size_t k = 0;
        bool placed = false;
        for (auto it = comps.begin(); it != comps.end(); ++it, ++k) {
            if (k == position_) {
                rebuilt[type_] = old_;
                placed = true;
            }
            rebuilt[it.key()] = it.value();
        }
        if (!placed) rebuilt[type_] = old_;
        comps = std::move(rebuilt);
    }

private:
    Guid id_;
    std::string type_;
    asset::Json new_, old_;
    bool had_ = false;
    std::size_t position_ = 0;
};

class AddEntity final : public Command {
public:
    explicit AddEntity(Entity e) : entity_(std::move(e)) {}
    std::string label() const override { return "Add " + entity_.name; }
    bool apply(Scene& s) override {
        if (index_of(s, entity_.id) >= 0) return false;  // ids are unique
        std::size_t at = s.entities.size();
        if (entity_.parent) {
            if (index_of(s, *entity_.parent) < 0) return false;
            const auto family = subtree(s, *entity_.parent);
            at = family.back() + 1;  // after the parent's last descendant: parents stay first
        }
        s.entities.insert(s.entities.begin() + static_cast<std::ptrdiff_t>(at), entity_);
        return true;
    }
    void revert(Scene& s) override {
        if (const auto i = index_of(s, entity_.id); i >= 0) s.entities.erase(s.entities.begin() + i);
    }

private:
    Entity entity_;
};

class RemoveEntity final : public Command {
public:
    explicit RemoveEntity(Guid id) : id_(id) {}
    std::string label() const override { return "Delete"; }
    bool apply(Scene& s) override {
        if (index_of(s, id_) < 0) return false;
        removed_.clear();
        for (const std::size_t i : subtree(s, id_)) removed_.push_back({i, s.entities[i]});
        for (auto it = removed_.rbegin(); it != removed_.rend(); ++it) {
            s.entities.erase(s.entities.begin() + static_cast<std::ptrdiff_t>(it->first));
        }
        return true;
    }
    void revert(Scene& s) override {
        for (const auto& [index, entity] : removed_) {  // ascending order restores original positions
            s.entities.insert(s.entities.begin() + static_cast<std::ptrdiff_t>(index), entity);
        }
    }

private:
    Guid id_;
    std::vector<std::pair<std::size_t, Entity>> removed_;
};

class Reparent final : public Command {
public:
    Reparent(Guid id, std::optional<Guid> parent) : id_(id), new_(parent) {}
    std::string label() const override { return "Reparent"; }
    bool apply(Scene& s) override {
        const auto i = index_of(s, id_);
        if (i < 0) return false;
        if (new_) {
            if (*new_ == id_ || index_of(s, *new_) < 0) return false;
            for (const std::size_t m : subtree(s, id_)) {
                if (s.entities[m].id == *new_) return false;  // would create a cycle
            }
        }
        // Remember the original order, then move the subtree right after the new parent's subtree.
        before_ = s.entities;
        old_ = s.entities[static_cast<std::size_t>(i)].parent;
        std::vector<Entity> moving;
        const auto family = subtree(s, id_);
        for (const std::size_t m : family) moving.push_back(s.entities[m]);
        for (auto it = family.rbegin(); it != family.rend(); ++it) {
            s.entities.erase(s.entities.begin() + static_cast<std::ptrdiff_t>(*it));
        }
        moving.front().parent = new_;
        std::size_t at = s.entities.size();
        if (new_) at = subtree(s, *new_).back() + 1;
        s.entities.insert(s.entities.begin() + static_cast<std::ptrdiff_t>(at), moving.begin(), moving.end());
        return true;
    }
    void revert(Scene& s) override { s.entities = before_; }

private:
    Guid id_;
    std::optional<Guid> new_, old_;
    std::vector<Entity> before_;
};

} // namespace

bool History::execute(Scene& scene, std::unique_ptr<Command> command) {
    if (!command || !command->apply(scene)) return false;
    redo_.clear();
    if (open_) {
        open_->commands.push_back(std::move(command));
        return true;
    }
    Step step{command->label(), {}, next_id_++};
    step.commands.push_back(std::move(command));
    undo_.push_back(std::move(step));
    if (undo_.size() > max_steps_) undo_.erase(undo_.begin());
    return true;
}

void History::begin_transaction(std::string label) {
    if (depth_++ == 0) open_ = Step{std::move(label), {}, next_id_++};
}

void History::end_transaction() {
    if (depth_ == 0 || --depth_ > 0) return;
    if (open_ && !open_->commands.empty()) {
        undo_.push_back(std::move(*open_));
        if (undo_.size() > max_steps_) undo_.erase(undo_.begin());
    }
    open_.reset();
}

bool History::undo(Scene& scene) {
    if (undo_.empty() || open_) return false;
    Step step = std::move(undo_.back());
    undo_.pop_back();
    for (auto it = step.commands.rbegin(); it != step.commands.rend(); ++it) (*it)->revert(scene);
    redo_.push_back(std::move(step));
    return true;
}

bool History::redo(Scene& scene) {
    if (redo_.empty() || open_) return false;
    Step step = std::move(redo_.back());
    redo_.pop_back();
    for (auto& c : step.commands) c->apply(scene);
    undo_.push_back(std::move(step));
    return true;
}

std::optional<std::string> History::undo_label() const {
    return undo_.empty() ? std::nullopt : std::optional<std::string>(undo_.back().label);
}

std::optional<std::string> History::redo_label() const {
    return redo_.empty() ? std::nullopt : std::optional<std::string>(redo_.back().label);
}

void History::clear() {
    undo_.clear();
    redo_.clear();
    open_.reset();
    depth_ = 0;
    saved_marker_ = 0;
}

std::unique_ptr<Command> set_transform(const Guid& e, const Transform& v) { return std::make_unique<SetTransform>(e, v); }
std::unique_ptr<Command> rename(const Guid& e, std::string n) { return std::make_unique<Rename>(e, std::move(n)); }
std::unique_ptr<Command> set_component(const Guid& e, std::string t, asset::Json v) {
    return std::make_unique<SetComponent>(e, std::move(t), std::move(v));
}
std::unique_ptr<Command> add_entity(Entity e) { return std::make_unique<AddEntity>(std::move(e)); }
std::unique_ptr<Command> remove_entity(const Guid& e) { return std::make_unique<RemoveEntity>(e); }
std::unique_ptr<Command> reparent(const Guid& e, std::optional<Guid> p) { return std::make_unique<Reparent>(e, p); }

} // namespace noor::scene
