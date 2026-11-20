#include "noor/asset/retarget.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <fstream>
#include <functional>

namespace noor::asset {
namespace {

using math::Quat;
using math::Vec3;
using H = HumanBone;

constexpr std::array<std::string_view, kHumanBones> kNames = {
    "Hips", "Spine", "Chest", "UpperChest", "Neck", "Head",
    "LeftShoulder", "LeftUpperArm", "LeftLowerArm", "LeftHand",
    "RightShoulder", "RightUpperArm", "RightLowerArm", "RightHand",
    "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "LeftToes",
    "RightUpperLeg", "RightLowerLeg", "RightFoot", "RightToes",
};

std::size_t at(H b) { return static_cast<std::size_t>(b); }

// The human bones a bone hangs from, nearest first (the first one mapped is its parent in the map).
std::vector<H> human_parents(H b) {
    switch (b) {
    case H::Hips: return {};
    case H::Spine: return {H::Hips};
    case H::Chest: return {H::Spine, H::Hips};
    case H::UpperChest: return {H::Chest, H::Spine, H::Hips};
    case H::Neck: return {H::UpperChest, H::Chest, H::Spine, H::Hips};
    case H::Head: return {H::Neck, H::UpperChest, H::Chest, H::Spine, H::Hips};
    case H::LeftShoulder: case H::RightShoulder: return {H::UpperChest, H::Chest, H::Spine, H::Hips};
    case H::LeftUpperArm: return {H::LeftShoulder, H::UpperChest, H::Chest, H::Spine, H::Hips};
    case H::RightUpperArm: return {H::RightShoulder, H::UpperChest, H::Chest, H::Spine, H::Hips};
    case H::LeftLowerArm: return {H::LeftUpperArm};
    case H::RightLowerArm: return {H::RightUpperArm};
    case H::LeftHand: return {H::LeftLowerArm};
    case H::RightHand: return {H::RightLowerArm};
    case H::LeftUpperLeg: case H::RightUpperLeg: return {H::Hips};
    case H::LeftLowerLeg: return {H::LeftUpperLeg};
    case H::RightLowerLeg: return {H::RightUpperLeg};
    case H::LeftFoot: return {H::LeftLowerLeg};
    case H::RightFoot: return {H::RightLowerLeg};
    case H::LeftToes: return {H::LeftFoot};
    case H::RightToes: return {H::RightFoot};
    default: return {};
    }
}

// What a bone points at: the first of these mapped (in both rigs). Head, hands and toes point at nothing we map.
std::vector<H> pointing_at(H b) {
    switch (b) {
    case H::Hips: return {H::Spine};
    case H::Spine: return {H::Chest, H::UpperChest, H::Neck, H::Head};
    case H::Chest: return {H::UpperChest, H::Neck, H::Head};
    case H::UpperChest: return {H::Neck, H::Head};
    case H::Neck: return {H::Head};
    case H::LeftShoulder: return {H::LeftUpperArm};
    case H::RightShoulder: return {H::RightUpperArm};
    case H::LeftUpperArm: return {H::LeftLowerArm};
    case H::RightUpperArm: return {H::RightLowerArm};
    case H::LeftLowerArm: return {H::LeftHand};
    case H::RightLowerArm: return {H::RightHand};
    case H::LeftUpperLeg: return {H::LeftLowerLeg};
    case H::RightUpperLeg: return {H::RightLowerLeg};
    case H::LeftLowerLeg: return {H::LeftFoot};
    case H::RightLowerLeg: return {H::RightFoot};
    case H::LeftFoot: return {H::LeftToes};
    case H::RightFoot: return {H::RightToes};
    default: return {};
    }
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// ---- Presets: exact names per bone (first found wins), after the preset's own clean-up of a name ----
struct Preset {
    std::string_view name;
    std::function<std::string(std::string_view)> clean;
    std::array<std::vector<std::string_view>, kHumanBones> names;
};

std::string after_colon(std::string_view s) {  // "mixamorig:LeftArm", "mixamorig1:LeftArm", "Armature_mixamorig_LeftArm"
    if (const auto c = s.rfind(':'); c != std::string_view::npos) return std::string(s.substr(c + 1));
    if (const auto m = s.find("mixamorig"); m != std::string_view::npos) {
        std::size_t p = m + 9;
        while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) ++p;
        if (p < s.size() && (s[p] == '_' || s[p] == '.')) return std::string(s.substr(p + 1));
    }
    return std::string(s);
}

std::array<std::vector<std::string_view>, kHumanBones> rigify_names(bool def) {
    std::array<std::vector<std::string_view>, kHumanBones> n;
    if (def) {
        n[at(H::Hips)] = {"DEF-spine"};
        n[at(H::Spine)] = {"DEF-spine.001"};
        n[at(H::Chest)] = {"DEF-spine.002"};
        n[at(H::UpperChest)] = {"DEF-spine.003"};
        n[at(H::Neck)] = {"DEF-spine.004"};
        n[at(H::Head)] = {"DEF-spine.006", "DEF-spine.005"};
        n[at(H::LeftShoulder)] = {"DEF-shoulder.L"};
        n[at(H::LeftUpperArm)] = {"DEF-upper_arm.L"};
        n[at(H::LeftLowerArm)] = {"DEF-forearm.L"};
        n[at(H::LeftHand)] = {"DEF-hand.L"};
        n[at(H::RightShoulder)] = {"DEF-shoulder.R"};
        n[at(H::RightUpperArm)] = {"DEF-upper_arm.R"};
        n[at(H::RightLowerArm)] = {"DEF-forearm.R"};
        n[at(H::RightHand)] = {"DEF-hand.R"};
        n[at(H::LeftUpperLeg)] = {"DEF-thigh.L"};
        n[at(H::LeftLowerLeg)] = {"DEF-shin.L"};
        n[at(H::LeftFoot)] = {"DEF-foot.L"};
        n[at(H::LeftToes)] = {"DEF-toe.L"};
        n[at(H::RightUpperLeg)] = {"DEF-thigh.R"};
        n[at(H::RightLowerLeg)] = {"DEF-shin.R"};
        n[at(H::RightFoot)] = {"DEF-foot.R"};
        n[at(H::RightToes)] = {"DEF-toe.R"};
    } else {
        n[at(H::Hips)] = {"spine"};
        n[at(H::Spine)] = {"spine.001"};
        n[at(H::Chest)] = {"spine.002"};
        n[at(H::UpperChest)] = {"spine.003"};
        n[at(H::Neck)] = {"spine.004"};
        n[at(H::Head)] = {"spine.006", "spine.005"};
        n[at(H::LeftShoulder)] = {"shoulder.L"};
        n[at(H::LeftUpperArm)] = {"upper_arm.L"};
        n[at(H::LeftLowerArm)] = {"forearm.L"};
        n[at(H::LeftHand)] = {"hand.L"};
        n[at(H::RightShoulder)] = {"shoulder.R"};
        n[at(H::RightUpperArm)] = {"upper_arm.R"};
        n[at(H::RightLowerArm)] = {"forearm.R"};
        n[at(H::RightHand)] = {"hand.R"};
        n[at(H::LeftUpperLeg)] = {"thigh.L"};
        n[at(H::LeftLowerLeg)] = {"shin.L"};
        n[at(H::LeftFoot)] = {"foot.L"};
        n[at(H::LeftToes)] = {"toe.L"};
        n[at(H::RightUpperLeg)] = {"thigh.R"};
        n[at(H::RightLowerLeg)] = {"shin.R"};
        n[at(H::RightFoot)] = {"foot.R"};
        n[at(H::RightToes)] = {"toe.R"};
    }
    return n;
}

const std::vector<Preset>& presets() {
    static const std::vector<Preset> list = [] {
        std::vector<Preset> p;
        const auto same = [](std::string_view s) { return std::string(s); };
        p.push_back({"Rigify", same, rigify_names(true)});
        std::array<std::vector<std::string_view>, kHumanBones> mixamo;
        mixamo[at(H::Hips)] = {"Hips"};
        mixamo[at(H::Spine)] = {"Spine"};
        mixamo[at(H::Chest)] = {"Spine1"};
        mixamo[at(H::UpperChest)] = {"Spine2"};
        mixamo[at(H::Neck)] = {"Neck"};
        mixamo[at(H::Head)] = {"Head"};
        for (const bool l : {true, false}) {
            mixamo[at(l ? H::LeftShoulder : H::RightShoulder)] = {l ? "LeftShoulder" : "RightShoulder"};
            mixamo[at(l ? H::LeftUpperArm : H::RightUpperArm)] = {l ? "LeftArm" : "RightArm"};
            mixamo[at(l ? H::LeftLowerArm : H::RightLowerArm)] = {l ? "LeftForeArm" : "RightForeArm"};
            mixamo[at(l ? H::LeftHand : H::RightHand)] = {l ? "LeftHand" : "RightHand"};
            mixamo[at(l ? H::LeftUpperLeg : H::RightUpperLeg)] = {l ? "LeftUpLeg" : "RightUpLeg"};
            mixamo[at(l ? H::LeftLowerLeg : H::RightLowerLeg)] = {l ? "LeftLeg" : "RightLeg"};
            mixamo[at(l ? H::LeftFoot : H::RightFoot)] = {l ? "LeftFoot" : "RightFoot"};
            mixamo[at(l ? H::LeftToes : H::RightToes)] = {l ? "LeftToeBase" : "RightToeBase"};
        }
        p.push_back({"Mixamo", after_colon, mixamo});
        std::array<std::vector<std::string_view>, kHumanBones> ue;
        ue[at(H::Hips)] = {"pelvis"};
        ue[at(H::Spine)] = {"spine_01"};
        ue[at(H::Chest)] = {"spine_02"};
        ue[at(H::UpperChest)] = {"spine_03"};
        ue[at(H::Neck)] = {"neck_01"};
        ue[at(H::Head)] = {"head"};
        for (const bool l : {true, false}) {
            ue[at(l ? H::LeftShoulder : H::RightShoulder)] = {l ? "clavicle_l" : "clavicle_r"};
            ue[at(l ? H::LeftUpperArm : H::RightUpperArm)] = {l ? "upperarm_l" : "upperarm_r"};
            ue[at(l ? H::LeftLowerArm : H::RightLowerArm)] = {l ? "lowerarm_l" : "lowerarm_r"};
            ue[at(l ? H::LeftHand : H::RightHand)] = {l ? "hand_l" : "hand_r"};
            ue[at(l ? H::LeftUpperLeg : H::RightUpperLeg)] = {l ? "thigh_l" : "thigh_r"};
            ue[at(l ? H::LeftLowerLeg : H::RightLowerLeg)] = {l ? "calf_l" : "calf_r"};
            ue[at(l ? H::LeftFoot : H::RightFoot)] = {l ? "foot_l" : "foot_r"};
            ue[at(l ? H::LeftToes : H::RightToes)] = {l ? "ball_l" : "ball_r"};
        }
        p.push_back({"Unreal mannequin", [](std::string_view s) { return lower(s); }, ue});
        p.push_back({"Blender metarig", same, rigify_names(false)});
        return p;
    }();
    return list;
}

// ---- Common names: a name's side and its letters, e.g. "DEF-upper_arm.L" -> left, "upperarm" ----
struct Words {
    int side = 0;  // 0 none, 1 left, 2 right
    std::string base;
};

Words words(std::string_view name) {
    std::string s = lower(after_colon(name));
    for (const std::string_view prefix : {"def-", "def_", "def."})
        if (s.starts_with(prefix)) s.erase(0, prefix.size());
    Words w;
    for (const std::string_view suffix : {".l", "_l", "-l", " l", ".left", "_left"})
        if (s.ends_with(suffix)) { w.side = 1; s.erase(s.size() - suffix.size()); break; }
    if (w.side == 0)
        for (const std::string_view suffix : {".r", "_r", "-r", " r", ".right", "_right"})
            if (s.ends_with(suffix)) { w.side = 2; s.erase(s.size() - suffix.size()); break; }
    if (w.side == 0) {
        for (const std::string_view prefix : {"left", "l_", "l."})
            if (s.starts_with(prefix)) { w.side = 1; s.erase(0, prefix.size()); break; }
        if (w.side == 0)
            for (const std::string_view prefix : {"right", "r_", "r."})
                if (s.starts_with(prefix)) { w.side = 2; s.erase(0, prefix.size()); break; }
    }
    for (const char c : s)
        if (std::isalnum(static_cast<unsigned char>(c))) w.base.push_back(c);
    while (!w.base.empty() && std::isdigit(static_cast<unsigned char>(w.base.back()))) w.base.pop_back();  // neck_01
    return w;
}

// The words each bone goes by, and whether it has a side.
struct Common {
    H bone;
    int side;
    std::vector<std::string_view> bases;
};
const std::vector<Common>& common_names() {
    static const std::vector<Common> list = [] {
        std::vector<Common> c = {
            {H::Hips, 0, {"hips", "hip", "pelvis"}},
            {H::Spine, 0, {"spine"}},
            {H::Chest, 0, {"chest"}},
            {H::UpperChest, 0, {"upperchest"}},
            {H::Neck, 0, {"neck"}},
            {H::Head, 0, {"head"}},
        };
        for (const int side : {1, 2}) {
            const bool l = side == 1;
            c.push_back({l ? H::LeftShoulder : H::RightShoulder, side, {"shoulder", "clavicle", "collar", "collarbone"}});
            c.push_back({l ? H::LeftUpperArm : H::RightUpperArm, side, {"upperarm", "uparm", "arm"}});
            c.push_back({l ? H::LeftLowerArm : H::RightLowerArm, side, {"forearm", "lowerarm"}});
            c.push_back({l ? H::LeftHand : H::RightHand, side, {"hand"}});
            c.push_back({l ? H::LeftUpperLeg : H::RightUpperLeg, side, {"thigh", "upleg", "upperleg"}});
            c.push_back({l ? H::LeftLowerLeg : H::RightLowerLeg, side, {"shin", "calf", "lowerleg", "leg"}});
            c.push_back({l ? H::LeftFoot : H::RightFoot, side, {"foot"}});
            c.push_back({l ? H::LeftToes : H::RightToes, side, {"toe", "toes", "toebase", "ball"}});
        }
        return c;
    }();
    return list;
}

// The nodes that can be bones: a skin's joints if the model has skins (meshes and helpers share names), else all.
std::vector<int> candidates(const ImportedModel& model) {
    std::vector<bool> joint(model.nodes.size(), model.skins.empty());
    for (const auto& skin : model.skins)
        for (const int j : skin.joints)
            if (j >= 0 && static_cast<std::size_t>(j) < joint.size()) joint[static_cast<std::size_t>(j)] = true;
    std::vector<int> depth(model.nodes.size(), 0), out;
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        const int p = model.nodes[i].parent;
        if (p >= 0) depth[i] = depth[static_cast<std::size_t>(p)] + 1;
        if (joint[i]) out.push_back(static_cast<int>(i));
    }
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) { return depth[static_cast<std::size_t>(a)] < depth[static_cast<std::size_t>(b)]; });
    return out;  // shallowest first
}

bool below(const ImportedModel& model, int node, int ancestor) {
    for (int p = model.nodes[static_cast<std::size_t>(node)].parent; p >= 0; p = model.nodes[static_cast<std::size_t>(p)].parent)
        if (p == ancestor) return true;
    return false;
}

void check(const ImportedModel& model, BoneMap& map) {
    for (std::size_t b = 0; b < kHumanBones; ++b) {
        const H bone = static_cast<H>(b);
        const int n = map.nodes[b];
        if (n < 0) {
            if (human_bone_required(bone)) map.warnings.push_back(std::format("no bone for {}", human_bone_name(bone)));
            continue;
        }
        for (std::size_t o = 0; o < b; ++o)
            if (map.nodes[o] == n)
                map.warnings.push_back(std::format("'{}' is mapped as both {} and {}", model.nodes[static_cast<std::size_t>(n)].name,
                                                   human_bone_name(static_cast<H>(o)), human_bone_name(bone)));
        for (const H p : human_parents(bone)) {
            const int pn = map.node(p);
            if (pn < 0) continue;
            if (!below(model, n, pn))
                map.warnings.push_back(std::format("{} ('{}') isn't below {} ('{}')", human_bone_name(bone),
                                                   model.nodes[static_cast<std::size_t>(n)].name, human_bone_name(p),
                                                   model.nodes[static_cast<std::size_t>(pn)].name));
            break;
        }
    }
}

bool complete(const BoneMap& map) {
    for (std::size_t b = 0; b < kHumanBones; ++b)
        if (human_bone_required(static_cast<H>(b)) && map.nodes[b] < 0) return false;
    return true;
}

Quat normalized(Quat q) {
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return len > 1e-12f ? Quat{q.x / len, q.y / len, q.z / len, q.w / len} : Quat{};
}

// The shortest turn taking direction a onto direction b (both unit). Square roots only: the same bits everywhere.
Quat shortest_arc(Vec3 a, Vec3 b) {
    const float d = math::dot(a, b);
    if (d < -0.999999f) {  // opposite: half a turn about any axis at right angles to a
        Vec3 axis = math::cross({1, 0, 0}, a);
        if (math::dot(axis, axis) < 1e-6f) axis = math::cross({0, 1, 0}, a);
        axis = math::normalize(axis);
        return {axis.x, axis.y, axis.z, 0.0f};
    }
    const Vec3 c = math::cross(a, b);
    return normalized({c.x, c.y, c.z, 1.0f + d});
}

} // namespace

std::string_view human_bone_name(HumanBone bone) { return at(bone) < kHumanBones ? kNames[at(bone)] : std::string_view("?"); }

std::optional<HumanBone> parse_human_bone(std::string_view name) {
    for (std::size_t i = 0; i < kHumanBones; ++i)
        if (kNames[i] == name) return static_cast<HumanBone>(i);
    return std::nullopt;
}

bool human_bone_required(HumanBone b) {
    switch (b) {
    case H::Chest: case H::UpperChest: case H::Neck: case H::LeftShoulder: case H::RightShoulder: case H::LeftToes: case H::RightToes:
        return false;
    default: return b != H::Count;
    }
}

bool BoneMap::usable() const { return complete(*this); }

BoneMap detect_bone_map(const ImportedModel& model) {
    const std::vector<int> nodes = candidates(model);
    for (const Preset& preset : presets()) {
        BoneMap map;
        map.preset = preset.name;
        for (std::size_t b = 0; b < kHumanBones; ++b)
            for (const std::string_view want : preset.names[b]) {
                const auto it = std::find_if(nodes.begin(), nodes.end(),
                                             [&](int n) { return preset.clean(model.nodes[static_cast<std::size_t>(n)].name) == want; });
                if (it != nodes.end()) {
                    map.nodes[b] = *it;
                    break;
                }
            }
        if (complete(map)) {
            check(model, map);
            return map;
        }
    }
    // Common names: the shallowest node going by each bone's words (and side), each node used once.
    BoneMap map;
    map.preset = "names";
    std::vector<Words> w;
    w.reserve(nodes.size());
    for (const int n : nodes) w.push_back(words(model.nodes[static_cast<std::size_t>(n)].name));
    std::vector<bool> used(nodes.size(), false);
    // Most specific words first ("forearm" before "arm", "upleg" before "leg"), bone by bone in that order.
    for (const bool loose : {false, true})
        for (const Common& c : common_names()) {
            if (map.nodes[at(c.bone)] >= 0) continue;
            for (std::size_t i = 0; i < nodes.size() && map.nodes[at(c.bone)] < 0; ++i) {
                if (used[i] || w[i].side != c.side) continue;
                const auto hit = std::find(c.bases.begin(), c.bases.end(), w[i].base);
                if (hit == c.bases.end()) continue;
                const bool is_loose = (*hit == "arm" || *hit == "leg");
                if (is_loose != loose) continue;
                map.nodes[at(c.bone)] = nodes[i];
                used[i] = true;
            }
        }
    if (!complete(map)) map.preset = "none";
    check(model, map);
    return map;
}

BoneMap bone_map(const ImportedModel& model, const Json& corrections) {
    BoneMap map = detect_bone_map(model);
    if (!corrections.is_object() || corrections.empty()) return map;
    map.warnings.clear();
    for (const auto& [key, value] : corrections.items()) {
        const auto bone = parse_human_bone(key);
        if (!bone) {
            map.warnings.push_back(std::format("the bone map names '{}', which isn't a humanoid bone", key));
            continue;
        }
        if (value.is_null()) {
            map.nodes[at(*bone)] = -1;
            continue;
        }
        if (!value.is_string()) continue;
        const std::string want = value.get<std::string>();
        int found = -1;
        for (std::size_t i = 0; i < model.nodes.size() && found < 0; ++i)
            if (model.nodes[i].name == want) found = static_cast<int>(i);
        if (found < 0) map.warnings.push_back(std::format("the bone map's {} is '{}', which the model doesn't have", key, want));
        else map.nodes[at(*bone)] = found;
    }
    map.preset += " (edited)";
    check(model, map);
    return map;
}

BoneMap bone_map_for(const ImportedModel& model, const std::filesystem::path& file) {
    std::ifstream in(file.string() + ".noormeta", std::ios::binary);
    if (!in) return detect_bone_map(model);
    const Json meta = Json::parse(in, nullptr, false);
    if (meta.is_discarded() || !meta.is_object() || !meta.contains("settings") || !meta["settings"].is_object() ||
        !meta["settings"].contains("bone_map"))
        return detect_bone_map(model);
    return bone_map(model, meta["settings"]["bone_map"]);
}

std::vector<math::Mat4> posed_world(const ImportedModel& model, std::span<const NodePose> pose) {
    std::vector<math::Mat4> out(model.nodes.size());
    for (std::size_t i = 0; i < model.nodes.size() && i < pose.size(); ++i) {
        const math::Mat4 local = math::compose(pose[i].translation, pose[i].rotation, pose[i].scale);
        const int p = model.nodes[i].parent;
        out[i] = p >= 0 ? out[static_cast<std::size_t>(p)] * local : local;
    }
    return out;
}

// ---- Retargeter ----

void Retargeter::globals(const ImportedModel& model, std::span<const NodePose> pose, std::vector<Xf>& out) {
    out.resize(model.nodes.size());
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        const NodePose& l = pose[i];
        const int p = model.nodes[i].parent;
        if (p < 0) {
            out[i] = {l.translation, l.rotation, l.scale};
            continue;
        }
        const Xf& P = out[static_cast<std::size_t>(p)];
        const Vec3 scaled{P.s.x * l.translation.x, P.s.y * l.translation.y, P.s.z * l.translation.z};
        out[i] = {P.t + math::rotate(P.r, scaled), normalized(P.r * l.rotation), {P.s.x * l.scale.x, P.s.y * l.scale.y, P.s.z * l.scale.z}};
    }
}

Retargeter::Retargeter(const ImportedModel& source, const BoneMap& smap, const ImportedModel& target, const BoneMap& tmap)
    : source_(&source), target_(&target) {
    for (const auto& w : smap.warnings) warnings_.push_back("the clips' model: " + w);
    for (const auto& w : tmap.warnings) warnings_.push_back("this model: " + w);
    usable_ = smap.usable() && tmap.usable();
    source_rest_pose_ = rest_pose(source);
    target_rest_pose_ = rest_pose(target);
    globals(source, source_rest_pose_, source_rest_);
    globals(target, target_rest_pose_, target_rest_);
    source_of_.assign(target.nodes.size(), -1);
    correction_.assign(target.nodes.size(), Quat{});
    if (!usable_) return;

    // Each bone's correction: the turn pointing it where the source's points at rest (in the enum's order, parents first,
    // so a bone with nothing to point at can take its parent's).
    std::array<std::optional<Quat>, kHumanBones> corr;
    for (std::size_t b = 0; b < kHumanBones; ++b) {
        const H bone = static_cast<H>(b);
        const int s = smap.nodes[b], t = tmap.nodes[b];
        if (s < 0 || t < 0) continue;
        source_of_[static_cast<std::size_t>(t)] = s;
        for (const H next : pointing_at(bone)) {
            const int s2 = smap.node(next), t2 = tmap.node(next);
            if (s2 < 0 || t2 < 0) continue;
            const Vec3 ds = source_rest_[static_cast<std::size_t>(s2)].t - source_rest_[static_cast<std::size_t>(s)].t;
            const Vec3 dt = target_rest_[static_cast<std::size_t>(t2)].t - target_rest_[static_cast<std::size_t>(t)].t;
            if (math::dot(ds, ds) > 1e-12f && math::dot(dt, dt) > 1e-12f) corr[b] = shortest_arc(math::normalize(dt), math::normalize(ds));
            break;
        }
        if (!corr[b])
            for (const H p : human_parents(bone))
                if (corr[at(p)]) {
                    corr[b] = corr[at(p)];
                    break;
                }
        correction_[static_cast<std::size_t>(t)] = corr[b].value_or(Quat{});
    }

    // The hips: moved by the source's movement, scaled by leg length (hips height above the middle of the feet).
    source_hips_ = smap.node(H::Hips);
    target_hips_ = tmap.node(H::Hips);
    const auto leg = [](const std::vector<Xf>& g, const BoneMap& m) {
        const float feet = 0.5f * (g[static_cast<std::size_t>(m.node(H::LeftFoot))].t.y + g[static_cast<std::size_t>(m.node(H::RightFoot))].t.y);
        return g[static_cast<std::size_t>(m.node(H::Hips))].t.y - feet;
    };
    const float ls = leg(source_rest_, smap), lt = leg(target_rest_, tmap);
    if (ls > 1e-4f && lt > 1e-4f) hips_scale_ = lt / ls;
    else warnings_.push_back("the hips aren't above the feet at rest in both models: the hips move unscaled");
}

void Retargeter::transfer(std::span<const NodePose> source_pose, std::span<NodePose> target_pose, const std::vector<bool>* only) const {
    if (!usable_) return;
    std::vector<Xf> sg, tg(target_->nodes.size());
    globals(*source_, source_pose, sg);
    for (std::size_t i = 0; i < target_->nodes.size() && i < target_pose.size(); ++i) {
        NodePose& p = target_pose[i];
        const int parent = target_->nodes[i].parent;
        const Xf P = parent >= 0 ? tg[static_cast<std::size_t>(parent)] : Xf{};
        const int s = source_of_[i];
        if (s >= 0 && (!only || (*only)[i])) {
            const auto su = static_cast<std::size_t>(s);
            const Quat turned = normalized(sg[su].r * math::conjugate(source_rest_[su].r));  // the source bone's turn from rest
            const Quat want = normalized(turned * correction_[i] * target_rest_[i].r);
            p.rotation = normalized(math::conjugate(P.r) * want);
            if (static_cast<int>(i) == target_hips_) {
                const Vec3 moved = (sg[su].t - source_rest_[su].t) * hips_scale_;
                const Vec3 world = target_rest_[i].t + moved;
                const Vec3 local = math::rotate(math::conjugate(P.r), world - P.t);
                p.translation = {local.x / P.s.x, local.y / P.s.y, local.z / P.s.z};
            }
        }
        const Vec3 scaled{P.s.x * p.translation.x, P.s.y * p.translation.y, P.s.z * p.translation.z};
        tg[i] = {P.t + math::rotate(P.r, scaled), normalized(P.r * p.rotation), {P.s.x * p.scale.x, P.s.y * p.scale.y, P.s.z * p.scale.z}};
    }
}

ImportedAnimation Retargeter::bake(const ImportedAnimation& clip, double rate) const {
    ImportedAnimation out;
    out.name = clip.name;
    out.duration = clip.duration;
    if (!usable_) return out;
    // A target bone is baked if the clip moves its source bone, or turns (or scales) anything above it: a hips that
    // only moves doesn't change where the legs point.
    std::vector<bool> moves(source_->nodes.size(), false), turns(source_->nodes.size(), false);
    for (const auto& ch : clip.channels) {
        if (ch.node < 0 || static_cast<std::size_t>(ch.node) >= moves.size()) continue;
        moves[static_cast<std::size_t>(ch.node)] = true;
        if (ch.path != AnimationChannel::Path::Translation) turns[static_cast<std::size_t>(ch.node)] = true;
    }
    std::vector<bool> baked(target_->nodes.size(), false);
    std::vector<int> nodes;
    for (std::size_t i = 0; i < target_->nodes.size(); ++i) {
        const int s = source_of_[i];
        if (s < 0) continue;
        baked[i] = moves[static_cast<std::size_t>(s)];
        for (int a = source_->nodes[static_cast<std::size_t>(s)].parent; a >= 0 && !baked[i]; a = source_->nodes[static_cast<std::size_t>(a)].parent)
            baked[i] = turns[static_cast<std::size_t>(a)];
        if (baked[i]) nodes.push_back(static_cast<int>(i));
    }
    if (nodes.empty()) return out;

    // Key times: every source key, and an even grid at least `rate` a second, ending exactly at the clip's end.
    std::vector<double> times;
    for (const auto& ch : clip.channels)
        for (const float t : ch.times) times.push_back(t);
    const double duration = clip.duration;
    const int steps = duration > 0.0 ? std::max(1, static_cast<int>(std::ceil(duration * rate - 1e-9))) : 0;
    for (int k = 0; k <= steps; ++k) times.push_back(steps > 0 ? duration * k / steps : 0.0);
    std::sort(times.begin(), times.end());
    std::vector<double> keys;
    for (const double t : times)
        if (keys.empty() || t - keys.back() > 1e-6) keys.push_back(t);

    std::vector<AnimationChannel> rot(nodes.size());
    AnimationChannel hips;
    hips.path = AnimationChannel::Path::Translation;
    hips.node = target_hips_;
    const bool hips_baked = target_hips_ >= 0 && baked[static_cast<std::size_t>(target_hips_)];
    for (std::size_t k = 0; k < nodes.size(); ++k) {
        rot[k].path = AnimationChannel::Path::Rotation;
        rot[k].node = nodes[k];
    }
    std::vector<NodePose> spose, tpose;
    for (const double t : keys) {
        spose = source_rest_pose_;
        sample(clip, t, spose);
        tpose = target_rest_pose_;
        transfer(spose, tpose, &baked);
        for (std::size_t k = 0; k < nodes.size(); ++k) {
            Quat q = tpose[static_cast<std::size_t>(nodes[k])].rotation;
            auto& v = rot[k].values;
            if (!v.empty() && q.x * v[v.size() - 4] + q.y * v[v.size() - 3] + q.z * v[v.size() - 2] + q.w * v[v.size() - 1] < 0.0f)
                q = {-q.x, -q.y, -q.z, -q.w};  // the same hemisphere as the key before: blends take the short way
            rot[k].times.push_back(static_cast<float>(t));
            v.insert(v.end(), {q.x, q.y, q.z, q.w});
        }
        if (hips_baked) {
            const Vec3 h = tpose[static_cast<std::size_t>(target_hips_)].translation;
            hips.times.push_back(static_cast<float>(t));
            hips.values.insert(hips.values.end(), {h.x, h.y, h.z});
        }
    }
    out.channels = std::move(rot);
    if (hips_baked) out.channels.push_back(std::move(hips));
    return out;
}

} // namespace noor::asset
