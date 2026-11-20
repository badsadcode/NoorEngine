#include "noor/game/animation_assets.h"
#include "noor/asset/model_import.h"

#include <cmath>
#include <format>
#include <unordered_set>

namespace noor::game {

using asset::Json;

namespace {

bool same_offset(const asset::ImportedNode& a, const asset::ImportedNode& b) {  // within a millimetre and a percent
    const math::Vec3 d{a.translation.x - b.translation.x, a.translation.y - b.translation.y, a.translation.z - b.translation.z};
    return math::length(d) <= 0.001f + 0.01f * math::length(b.translation);
}
bool same_turn(const math::Quat& a, const math::Quat& b) {  // within about 2.5 degrees
    return std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) >= 0.99976f;
}
bool same_scale(const math::Vec3& a, const math::Vec3& b) {
    return std::abs(a.x - b.x) <= 0.01f * std::abs(b.x) + 1e-6f && std::abs(a.y - b.y) <= 0.01f * std::abs(b.y) + 1e-6f &&
           std::abs(a.z - b.z) <= 0.01f * std::abs(b.z) + 1e-6f;
}

} // namespace

std::optional<std::vector<asset::ImportedAnimation>> same_skeleton_clips(const asset::ImportedModel& source, const asset::ImportedModel& target) {
    std::unordered_map<std::string, int> by_name;
    for (std::size_t i = 0; i < target.nodes.size(); ++i) by_name.emplace(target.nodes[i].name, static_cast<int>(i));
    std::unordered_set<int> animated, translated;  // bones the clips move; those whose position they move (no fixed offset)
    for (const auto& clip : source.animations)
        for (const auto& ch : clip.channels)
            if (ch.node >= 0 && static_cast<std::size_t>(ch.node) < source.nodes.size()) {
                animated.insert(ch.node);
                if (ch.path != asset::AnimationChannel::Path::Translation) continue;
                // Moved for real: a key away from the rest offset (Blender's FBX exporter keys every bone's position,
                // most of them standing still).
                const auto& rest = source.nodes[static_cast<std::size_t>(ch.node)].translation;
                for (std::size_t k = 0; k + 2 < ch.values.size(); k += 3)
                    if (std::abs(ch.values[k] - rest.x) + std::abs(ch.values[k + 1] - rest.y) + std::abs(ch.values[k + 2] - rest.z) >
                        1e-4f * (1.0f + math::length(rest))) {
                        translated.insert(ch.node);
                        break;
                    }
            }
    if (animated.empty()) return std::nullopt;

    // What the clips' values are relative to must be the same in both. (1) The nodes above the animated bones that the
    // clips don't move (an armature, a root): the same names and transforms. Blender 3.3 puts its axis and unit
    // conversion on the armature, 5.2 doesn't, so 3.3's hips movement means something else on a 5.2 skeleton.
    std::unordered_set<int> above;
    for (const int n : animated)
        for (int p = source.nodes[static_cast<std::size_t>(n)].parent; p >= 0 && !animated.contains(p); p = source.nodes[static_cast<std::size_t>(p)].parent)
            above.insert(p);
    for (const int n : above) {
        const auto& sn = source.nodes[static_cast<std::size_t>(n)];
        const auto it = by_name.find(sn.name);
        if (it == by_name.end()) continue;  // a node the target hasn't got (an exporter's extra root): nothing to compare
        const auto& tn = target.nodes[static_cast<std::size_t>(it->second)];
        if (!same_offset(sn, tn) || !same_turn(sn.rotation, tn.rotation) || !same_scale(sn.scale, tn.scale)) return std::nullopt;
    }
    // (2) Each animated bone: the same name and the same offset from its parent. Animation turns bones without moving
    // those offsets, so a clip-only file's stored pose doesn't matter. A bone whose position the clips move has no fixed
    // offset: its name is enough. Where the clips' file has a real rest pose (it's skinned), the rest rotations must
    // agree too (a clip-only file's stored pose is whatever its exporter left there). Nine in ten must pass.
    const bool source_has_rest = !source.skins.empty();
    std::size_t found = 0;
    for (const int n : animated) {
        const auto& sn = source.nodes[static_cast<std::size_t>(n)];
        const auto it = by_name.find(sn.name);
        if (it == by_name.end()) continue;
        const auto& tn = target.nodes[static_cast<std::size_t>(it->second)];
        if (translated.contains(n) || (same_offset(sn, tn) && (!source_has_rest || same_turn(sn.rotation, tn.rotation)))) ++found;
    }
    if (found * 10 < animated.size() * 9) return std::nullopt;  // not the same skeleton: retarget instead

    std::vector<asset::ImportedAnimation> out;
    for (const auto& clip : source.animations) {
        asset::ImportedAnimation copy{clip.name, {}, clip.duration};
        for (const auto& ch : clip.channels) {
            if (ch.node < 0 || static_cast<std::size_t>(ch.node) >= source.nodes.size()) continue;
            const auto it = by_name.find(source.nodes[static_cast<std::size_t>(ch.node)].name);
            if (it == by_name.end()) continue;
            copy.channels.push_back(ch);
            copy.channels.back().node = it->second;
        }
        out.push_back(std::move(copy));
    }
    return out;
}

const AnimationAssets::Model* AnimationAssets::model(const Guid& id, const Guid& clips) {
    const std::string key = clips.is_nil() ? id.to_string() : id.to_string() + "<" + clips.to_string();
    auto it = models_.find(key);
    if (it != models_.end()) return it->second ? &*it->second : nullptr;
    std::optional<Model> loaded;
    if (const auto file = resolve_ ? resolve_(id) : std::nullopt) {
        if (auto m = asset::import_model(*file)) {
            Model data{std::move(*m), {}, {}};
            const asset::BoneMap own = asset::bone_map_for(data.model, *file);
            for (std::size_t b = 0; b < asset::kHumanBones; ++b)
                if (own.nodes[b] >= 0) data.aliases.emplace(asset::human_bone_name(static_cast<asset::HumanBone>(b)), own.nodes[b]);
            if (!clips.is_nil()) {
                // Another model's clips, retargeted onto this one (once per pair).
                const auto source_file = resolve_(clips);
                auto source = source_file ? asset::import_model(*source_file) : decltype(asset::import_model(*file)){};
                if (!source_file) {
                    warn(std::format("an animator's retarget_from {} isn't in the project", clips.to_string()));
                    data.model.animations.clear();
                } else if (!source) {
                    warn(source.error().message);
                    data.model.animations.clear();
                } else if (const auto copied = same_skeleton_clips(*source, data.model)) {
                    // The same skeleton (part 7): a character's clips in files of their own, as FBX packs and
                    // Mixamo ship them, on any skeleton (a fish's too). The clips are copied, by bone name.
                    data.model.animations = *copied;
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
