#include "noor/game/animation_assets.h"

#include <format>
#include <unordered_set>

namespace noor::game {

using asset::Json;

const AnimationAssets::Model* AnimationAssets::model(const Guid& id, const Guid& clips) {
    const std::string key = clips.is_nil() ? id.to_string() : id.to_string() + "<" + clips.to_string();
    auto it = models_.find(key);
    if (it != models_.end()) return it->second ? &*it->second : nullptr;
    std::optional<Model> loaded;
    if (const auto file = resolve_ ? resolve_(id) : std::nullopt) {
        if (auto m = asset::import_gltf(*file)) {
            Model data{std::move(*m), {}, {}};
            const asset::BoneMap own = asset::bone_map_for(data.model, *file);
            for (std::size_t b = 0; b < asset::kHumanBones; ++b)
                if (own.nodes[b] >= 0) data.aliases.emplace(asset::human_bone_name(static_cast<asset::HumanBone>(b)), own.nodes[b]);
            if (!clips.is_nil()) {
                // Another model's clips, retargeted onto this one (once per pair).
                const auto source_file = resolve_(clips);
                auto source = source_file ? asset::import_gltf(*source_file) : decltype(asset::import_gltf(*file)){};
                if (!source_file) {
                    warn(std::format("an animator's retarget_from {} isn't in the project", clips.to_string()));
                    data.model.animations.clear();
                } else if (!source) {
                    warn(source.error().message);
                    data.model.animations.clear();
                } else {
                    const asset::BoneMap theirs = asset::bone_map_for(*source, *source_file);
                    const asset::Retargeter retargeter(*source, theirs, data.model, own);
                    // A controller written for the clips' model names its bones: they mean this model's.
                    for (std::size_t b = 0; b < asset::kHumanBones; ++b)
                        if (theirs.nodes[b] >= 0 && own.nodes[b] >= 0)
                            data.aliases.emplace(source->nodes[static_cast<std::size_t>(theirs.nodes[b])].name, own.nodes[b]);
                    for (const auto& w : retargeter.warnings())
                        warn(std::format("retargeting {}'s clips onto {}: {}", source_file->filename().string(), file->filename().string(), w));
                    std::vector<asset::ImportedAnimation> baked;
                    if (retargeter.usable())
                        for (const auto& clip : source->animations) baked.push_back(retargeter.bake(clip));
                    data.model.animations = std::move(baked);
                }
            }
            data.rest = asset::rest_pose(data.model);
            data.root = asset::root_motion_node(data.model);
            if (data.root >= 0)
                if (const int parent = data.model.nodes[static_cast<std::size_t>(data.root)].parent; parent >= 0)
                    data.root_space = data.model.world_matrix(static_cast<std::size_t>(parent));
            std::vector<bool> moved(data.model.nodes.size(), false);
            for (const auto& clip : data.model.animations)
                for (const auto& ch : clip.channels)
                    if (ch.node >= 0 && static_cast<std::size_t>(ch.node) < moved.size()) moved[static_cast<std::size_t>(ch.node)] = true;
            for (std::size_t i = 0; i < moved.size(); ++i)
                if (moved[i]) data.animated.push_back(static_cast<int>(i));
            loaded = std::move(data);
        } else {
            warn(m.error().message);
        }
    } else {
        warn(std::format("an animator's model {} isn't in the project", id.to_string()));
    }
    it = models_.emplace(key, std::move(loaded)).first;
    return it->second ? &*it->second : nullptr;
}

const AnimationAssets::Model* AnimationAssets::model_of(const scene::Entity& root) {
    if (!root.has("model")) return nullptr;
    const auto id = asset::guid_field(root.components.at("model"), "asset");
    if (!id) return nullptr;
    const auto clips = root.has("animator") ? asset::guid_field(root.components.at("animator"), "retarget_from") : std::nullopt;
    return model(*id, clips.value_or(Guid{}));
}

const asset::AnimController* AnimationAssets::controller(const Guid& id) {
    auto it = controllers_.find(id);
    if (it == controllers_.end()) {
        std::optional<asset::AnimController> loaded;
        if (const auto file = resolve_ ? resolve_(id) : std::nullopt) {
            if (auto c = asset::load_anim_controller(*file)) loaded = std::move(*c);
            else warn(c.error().message);
        } else {
            warn(std::format("an animator's controller {} isn't in the project", id.to_string()));
        }
        it = controllers_.emplace(id, std::move(loaded)).first;
    }
    return it->second ? &*it->second : nullptr;
}

std::vector<Guid> model_node_entities(const scene::Scene& scene, const scene::Entity& root, const asset::ImportedModel& model, bool* by_name) {
    std::vector<Guid> ids;
    if (by_name) *by_name = false;
    const Json& m = root.components.at("model");
    if (m.contains("nodes") && m["nodes"].is_array()) {
        for (const auto& n : m["nodes"]) {
            Guid g{};
            if (n.is_string())
                if (auto parsed = Guid::parse(n.get<std::string>())) g = *parsed;
            ids.push_back(g);
        }
        return ids;
    }
    // Placed before 8C part 2: no node list. Match the model's nodes by name among the root's descendants (entities
    // come parents first, so one pass finds them all).
    if (by_name) *by_name = true;
    std::unordered_set<Guid> below{root.id};
    std::unordered_map<std::string, Guid> names;
    for (const auto& e : scene.entities) {
        if (!e.parent || !below.contains(*e.parent)) continue;
        below.insert(e.id);
        names.emplace(e.name, e.id);
    }
    for (const auto& node : model.nodes) {
        const auto f = names.find(node.name);
        ids.push_back(f != names.end() ? f->second : Guid{});
    }
    return ids;
}

} // namespace noor::game
