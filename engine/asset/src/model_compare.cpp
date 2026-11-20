#include "noor/asset/model_compare.h"

#include "noor/asset/animation.h"
#include "noor/asset/retarget.h"
#include "noor/asset/skinning.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace noor::asset {
namespace {
using math::Vec3;

bool leaf_end(const std::string& name) {
    return name.size() > 4 && (name.ends_with("_end") || name.ends_with("_End") || name.ends_with(".end"));
}

// Every drawn point of the model in this pose (model space): skinned meshes by their joints, others by their node.
std::vector<Vec3> shape(const ImportedModel& m, const std::vector<math::Mat4>& worlds, std::size_t max_points) {
    std::size_t total = 0;
    for (const auto& n : m.nodes)
        if (n.mesh >= 0 && !n.is_collision()) total += m.meshes[static_cast<std::size_t>(n.mesh)].vertices.size();
    const std::size_t stride = std::max<std::size_t>(1, (total + max_points - 1) / std::max<std::size_t>(1, max_points));
    std::vector<Vec3> out;
    out.reserve(total / stride + 1);
    std::size_t counter = 0;
    for (std::size_t i = 0; i < m.nodes.size(); ++i) {
        const ImportedNode& n = m.nodes[i];
        if (n.mesh < 0 || n.is_collision()) continue;
        const ImportedMesh& mesh = m.meshes[static_cast<std::size_t>(n.mesh)];
        std::vector<math::Mat4> palette;
        const bool skinned = n.skin >= 0 && mesh.skin.size() == mesh.vertices.size();
        if (skinned) {
            const ImportedSkin& skin = m.skins[static_cast<std::size_t>(n.skin)];
            std::vector<math::Mat4> joints;
            for (const int j : skin.joints) joints.push_back(worlds[static_cast<std::size_t>(j)]);
            palette = skin_palette(skin, joints);
        }
        for (std::size_t v = 0; v < mesh.vertices.size(); ++v) {
            if (counter++ % stride != 0) continue;
            out.push_back(skinned ? skin_point(mesh.skin[v], palette, mesh.vertices[v].position) : worlds[i].transform_point(mesh.vertices[v].position));
        }
    }
    return out;
}

// The farthest any point of `a` is from the nearest point of `b` (a grid of cells; beyond four rings of cells the
// distance is given as at least that far).
float one_way(const std::vector<Vec3>& a, const std::vector<Vec3>& b) {
    if (a.empty() || b.empty()) return a.empty() && b.empty() ? 0.0f : 1e9f;
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const auto& p : b) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    const float diag = math::length(hi - lo);
    const float cell = std::max(1e-3f, diag / 64.0f);
    const auto key = [&](int x, int y, int z) { return (static_cast<std::int64_t>(x) * 73856093) ^ (static_cast<std::int64_t>(y) * 19349663) ^ (static_cast<std::int64_t>(z) * 83492791); };
    std::unordered_map<std::int64_t, std::vector<std::uint32_t>> grid;
    const auto cell_of = [&](const Vec3& p) {
        return std::array<int, 3>{static_cast<int>(std::floor(p.x / cell)), static_cast<int>(std::floor(p.y / cell)), static_cast<int>(std::floor(p.z / cell))};
    };
    for (std::uint32_t i = 0; i < b.size(); ++i) {
        const auto c = cell_of(b[i]);
        grid[key(c[0], c[1], c[2])].push_back(i);
    }
    float worst = 0.0f;
    for (const auto& p : a) {
        const auto c = cell_of(p);
        float best = 1e30f;
        for (int ring = 0; ring <= 4 && best > static_cast<float>(ring - 1) * cell; ++ring) {
            for (int x = -ring; x <= ring; ++x)
                for (int y = -ring; y <= ring; ++y)
                    for (int z = -ring; z <= ring; ++z) {
                        if (std::max({std::abs(x), std::abs(y), std::abs(z)}) != ring) continue;
                        const auto it = grid.find(key(c[0] + x, c[1] + y, c[2] + z));
                        if (it == grid.end()) continue;
                        for (const auto i : it->second) best = std::min(best, math::length(b[i] - p));
                    }
        }
        worst = std::max(worst, std::min(best, 5.0f * cell));
    }
    return worst;
}

// Both ways: every sampled point of one against every point of the other (the two sides' samples are different points).
float shape_distance(const std::vector<Vec3>& a_sampled, const std::vector<Vec3>& a_all, const std::vector<Vec3>& b_sampled, const std::vector<Vec3>& b_all) {
    return std::max(one_way(a_sampled, b_all), one_way(b_sampled, a_all));
}

} // namespace

float ModelComparison::worst_joints() const {
    float w = rest_joints;
    for (const auto& c : clips) w = std::max(w, c.joints);
    return w;
}

float ModelComparison::worst_shape() const {
    float w = rest_shape;
    for (const auto& c : clips) w = std::max(w, c.shape);
    return w;
}

Json ModelComparison::to_json() const {
    Json j{{"nodes_a", nodes_a},
           {"nodes_b", nodes_b},
           {"matched", matched},
           {"only_a", only_a},
           {"only_b", only_b},
           {"parent_differs", parent_differs},
           {"triangles_a", triangles_a},
           {"triangles_b", triangles_b},
           {"skins_a", skins_a},
           {"skins_b", skins_b},
           {"joints_a", joints_a},
           {"joints_b", joints_b},
           {"rest_joints_m", rest_joints},
           {"rest_joints_worst", rest_joints_worst},
           {"rest_shape_m", rest_shape},
           {"worst_joints_m", worst_joints()},
           {"worst_shape_m", worst_shape()},
           {"clips_only_a", clips_only_a},
           {"clips_only_b", clips_only_b}};
    Json cs = Json::array();
    for (const auto& c : clips)
        cs.push_back({{"name", c.name}, {"duration_a", c.duration_a}, {"duration_b", c.duration_b}, {"offset_b", c.offset_b}, {"samples", c.samples}, {"joints_m", c.joints},
                      {"joints_worst", c.joints_worst}, {"joints_worst_time", c.joints_worst_time}, {"shape_m", c.shape}});
    j["clips"] = std::move(cs);
    return j;
}

ModelComparison compare_models(const ImportedModel& a, const ImportedModel& b, const CompareOptions& options) {
    ModelComparison r;
    r.nodes_a = a.nodes.size();
    r.nodes_b = b.nodes.size();
    r.triangles_a = a.render_triangles();
    r.triangles_b = b.render_triangles();
    r.skins_a = a.skins.size();
    r.skins_b = b.skins.size();
    for (const auto& s : a.skins) r.joints_a += s.joints.size();
    for (const auto& s : b.skins) r.joints_b += s.joints.size();

    // Nodes by name, a mesh with a mesh and a node without one with another (Blender lets a bone and an object share a
    // name: the robot's "Torso" is both); the first of each.
    const auto key = [](const ImportedNode& n) { return n.mesh >= 0 ? n.name + " (mesh)" : n.name; };
    std::unordered_map<std::string, int> in_b;
    for (std::size_t i = 0; i < b.nodes.size(); ++i) in_b.emplace(key(b.nodes[i]), static_cast<int>(i));
    std::vector<int> twin(a.nodes.size(), -1);
    std::vector<bool> b_matched(b.nodes.size(), false);
    for (std::size_t i = 0; i < a.nodes.size(); ++i) {
        const auto it = in_b.find(key(a.nodes[i]));
        if (it != in_b.end() && !b_matched[static_cast<std::size_t>(it->second)]) {
            twin[i] = it->second;
            b_matched[static_cast<std::size_t>(it->second)] = true;
            ++r.matched;
        } else if (!(options.ignore_leaf_ends && leaf_end(a.nodes[i].name))) {
            r.only_a.push_back(a.nodes[i].name);
        }
    }
    for (std::size_t i = 0; i < b.nodes.size(); ++i)
        if (!b_matched[i] && !(options.ignore_leaf_ends && leaf_end(b.nodes[i].name))) r.only_b.push_back(b.nodes[i].name);
    const auto matched_ancestor_a = [&](int n) {
        for (int p = a.nodes[static_cast<std::size_t>(n)].parent; p >= 0; p = a.nodes[static_cast<std::size_t>(p)].parent)
            if (twin[static_cast<std::size_t>(p)] >= 0) return twin[static_cast<std::size_t>(p)];
        return -1;
    };
    const auto matched_ancestor_b = [&](int n) {
        for (int p = b.nodes[static_cast<std::size_t>(n)].parent; p >= 0; p = b.nodes[static_cast<std::size_t>(p)].parent)
            if (b_matched[static_cast<std::size_t>(p)]) return p;
        return -1;
    };
    for (std::size_t i = 0; i < a.nodes.size(); ++i)
        if (twin[i] >= 0 && matched_ancestor_a(static_cast<int>(i)) != matched_ancestor_b(twin[i])) r.parent_differs.push_back(a.nodes[i].name);

    // Joints compared: the nodes that move something drawn, in either model: skin joints, nodes carrying a mesh that
    // isn't skinned, and nodes a clip animates. (A skinned mesh's own node and a bone that moves no vertex can sit
    // anywhere: importers place them differently and nothing shows.)
    std::vector<bool> shows(a.nodes.size(), false), shows_at_rest(a.nodes.size(), false);
    {
        std::vector<bool> shows_b(b.nodes.size(), false);
        std::vector<bool> rest_b(b.nodes.size(), false);
        const auto mark = [](const ImportedModel& m, std::vector<bool>& out, std::vector<bool>& at_rest) {
            for (const auto& s : m.skins)
                for (const int j : s.joints) out[static_cast<std::size_t>(j)] = at_rest[static_cast<std::size_t>(j)] = true;
            for (std::size_t i = 0; i < m.nodes.size(); ++i)
                if (m.nodes[i].mesh >= 0 && m.nodes[i].skin < 0) out[i] = at_rest[i] = true;
            for (const auto& c : m.animations)
                for (const auto& ch : c.channels)
                    if (ch.node >= 0) out[static_cast<std::size_t>(ch.node)] = true;
        };
        mark(a, shows, shows_at_rest);
        mark(b, shows_b, rest_b);
        for (std::size_t i = 0; i < a.nodes.size(); ++i)
            if (twin[i] >= 0) {
                if (shows_b[static_cast<std::size_t>(twin[i])]) shows[i] = true;
                if (rest_b[static_cast<std::size_t>(twin[i])]) shows_at_rest[i] = true;
            }
    }
    const auto joints = [&](const std::vector<math::Mat4>& wa, const std::vector<math::Mat4>& wb, std::string& worst_name, bool at_rest = false) {
        float worst = 0.0f;
        for (std::size_t i = 0; i < a.nodes.size(); ++i) {
            if (twin[i] < 0 || !(at_rest ? shows_at_rest[i] : shows[i])) continue;
            if (options.ignore_leaf_ends && leaf_end(a.nodes[i].name)) continue;  // never weighted: where they sit doesn't show
            const float d = math::length(wa[i].transform_point({0, 0, 0}) - wb[static_cast<std::size_t>(twin[i])].transform_point({0, 0, 0}));
            if (d > worst) {
                worst = d;
                worst_name = a.nodes[i].name;
            }
        }
        return worst;
    };
    std::vector<NodePose> pa = rest_pose(a), pb = rest_pose(b);
    {
        const auto wa = posed_world(a, pa), wb = posed_world(b, pb);
        r.rest_joints = joints(wa, wb, r.rest_joints_worst, true);
        r.rest_shape = shape_distance(shape(a, wa, options.max_points), shape(a, wa, SIZE_MAX), shape(b, wb, options.max_points), shape(b, wb, SIZE_MAX));
    }

    // Clips by name; failing that, one whose name is the other's with "_<something>" after it (Blender 3.3's glTF
    // importer names actions "Walk_Ward", which its FBX exporter keeps and its glTF exporter doesn't).
    const auto parts = [](const std::string& name) {  // Blender's FBX importer: "Armature|idle|BaseLayer"
        std::vector<std::string> out;
        std::size_t start = 0;
        for (std::size_t bar = name.find('|'); bar != std::string::npos; bar = name.find('|', start)) {
            out.push_back(name.substr(start, bar - start));
            start = bar + 1;
        }
        out.push_back(name.substr(start));
        return out;
    };
    const auto twin_clip = [&](const ImportedModel& m, const std::string& name) -> const ImportedAnimation* {
        if (const auto* c = find_animation(m, name)) return c;
        const ImportedModel& other = &m == &b ? a : b;
        if (m.animations.size() == 1 && other.animations.size() == 1) return &m.animations[0];  // one clip each: those two
        for (const auto& c : m.animations)
            for (const auto& p : parts(c.name))
                if (p == name) return &c;
        for (const auto& p : parts(name))
            if (const auto* c = find_animation(m, p)) return c;
        const ImportedAnimation* found = nullptr;
        for (const auto& c : m.animations)
            if ((c.name.size() > name.size() && c.name.starts_with(name) && c.name[name.size()] == '_') ||
                (name.size() > c.name.size() && name.starts_with(c.name) && name[c.name.size()] == '_')) {
                if (found) return nullptr;  // more than one: none
                found = &c;
            }
        return found;
    };
    for (const auto& ca : a.animations) {
        const auto* cb = twin_clip(b, ca.name);
        if (!cb) {
            r.clips_only_a.push_back(ca.name);
            continue;
        }
        // Aligned at their starts; if one is longer, also at their ends (Blender keeps a take's start time: its clip
        // holds still until the take begins), and the better of the two is kept.
        const auto run = [&](double offset_b) {
            ModelComparison::Clip c;
            c.name = ca.name;
            c.duration_a = ca.duration;
            c.duration_b = cb->duration;
            c.offset_b = offset_b;
            const double length = offset_b > 0.0 ? ca.duration : std::max<double>(ca.duration, cb->duration);
            const int steps = std::max(1, static_cast<int>(std::ceil(length * options.sample_rate)));
            const int shape_every = std::max(1, steps / std::max(1, options.shape_samples));
            for (int s = 0; s <= steps; ++s) {
                const double time = length * s / steps;
                std::vector<NodePose> qa = rest_pose(a), qb = rest_pose(b);
                sample(ca, time, qa);
                sample(*cb, time + offset_b, qb);
                const auto wa = posed_world(a, qa), wb = posed_world(b, qb);
                std::string name;
                const float d = joints(wa, wb, name);
                if (d > c.joints) {
                    c.joints = d;
                    c.joints_worst = name;
                    c.joints_worst_time = time;
                }
                if (s % shape_every == 0 && s > 0)
                    c.shape = std::max(c.shape, shape_distance(shape(a, wa, options.max_points), shape(a, wa, SIZE_MAX), shape(b, wb, options.max_points),
                                                               shape(b, wb, SIZE_MAX)));
                ++c.samples;
            }
            return c;
        };
        ModelComparison::Clip c = run(0.0);
        if (cb->duration > ca.duration + 0.01f) {
            ModelComparison::Clip ends = run(cb->duration - ca.duration);
            if (ends.joints + ends.shape < c.joints + c.shape) c = std::move(ends);
        }
        r.clips.push_back(std::move(c));
    }
    for (const auto& cb : b.animations)
        if (!twin_clip(a, cb.name)) r.clips_only_b.push_back(cb.name);
    return r;
}

} // namespace noor::asset
