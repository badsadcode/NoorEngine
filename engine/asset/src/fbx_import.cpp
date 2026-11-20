#include "noor/asset/fbx_import.h"

#include "mesh_util.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <ufbx.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace noor::asset {
namespace {
namespace stdfs = std::filesystem;
using math::Vec3;

struct SceneDeleter {
    void operator()(ufbx_scene* s) const { ufbx_free_scene(s); }
};
struct BakeDeleter {
    void operator()(ufbx_baked_anim* b) const { ufbx_free_baked_anim(b); }
};

std::string text(const ufbx_string& s) { return std::string(s.data, s.length); }

std::string describe(const ufbx_error& error) {
    char buffer[1024];
    const size_t n = ufbx_format_error(buffer, sizeof buffer, &error);
    std::string out(buffer, n);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

math::Mat4 to_mat4(const ufbx_matrix& m) {
    math::Mat4 out = math::Mat4::identity();
    for (int c = 0; c < 4; ++c)
        out.m[c] = {static_cast<float>(m.cols[c].x), static_cast<float>(m.cols[c].y), static_cast<float>(m.cols[c].z), c == 3 ? 1.0f : 0.0f};
    return out;
}

Vec3 to_vec3(const ufbx_vec3& v) { return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }
math::Quat to_quat(const ufbx_quat& q) { return {static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z), static_cast<float>(q.w)}; }

// Blender names takes "<object>|<action>": the action's name is the clip's.
std::string clip_name(const std::string& take, std::size_t index) {
    const auto bar = take.rfind('|');
    std::string name = bar == std::string::npos ? take : take.substr(bar + 1);
    return name.empty() ? std::format("Take {}", index + 1) : name;
}

struct VertexKey {
    std::array<std::uint32_t, 4> k;  // logical vertex, normal, uv, colour (attribute indices)
    bool operator==(const VertexKey&) const = default;
};
struct VertexKeyHash {
    std::size_t operator()(const VertexKey& v) const noexcept {
        std::uint64_t h = 0xcbf29ce484222325ull;
        for (const auto x : v.k) {
            h ^= x;
            h *= 0x100000001b3ull;
        }
        return static_cast<std::size_t>(h);
    }
};

// What skin reading had to fix in a mesh, reported once per mesh in plain words (as glTF import does).
struct SkinStats {
    std::size_t over_four = 0, dropped = 0, renormalised = 0, weightless = 0;
};

} // namespace

Result<ImportedModel> import_fbx(const stdfs::path& path, std::uint64_t max_file_bytes) {
    std::error_code ec;
    const auto size = stdfs::file_size(path, ec);
    if (ec) return fail(ErrorCode::NotFound, std::format("Cannot read '{}': {}", path.generic_string(), ec.message()));
    if (size > max_file_bytes)
        return fail(ErrorCode::InvalidArgument, std::format("'{}' is {} bytes, above the import limit of {}", path.generic_string(), size, max_file_bytes));

    ufbx_load_opts opts{};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;              // geometry, transforms, skins and keys alike
    opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_HELPER_NODES;  // 3ds Max's offsets: a child node
    opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_COMPENSATE;         // Maya's segment scale compensation
    opts.generate_missing_normals = true;
    opts.clean_skin_weights = true;
    const auto file = path.u8string();
    ufbx_error error{};
    std::unique_ptr<ufbx_scene, SceneDeleter> scene(ufbx_load_file(reinterpret_cast<const char*>(file.c_str()), &opts, &error));
    if (!scene) return fail(ErrorCode::ParseError, std::format("'{}': {}", path.generic_string(), describe(error)));

    ImportedModel model;
    for (std::size_t i = 0; i < scene->metadata.warnings.count && i < 8; ++i)
        model.warnings.push_back(std::format("FBX: {}", text(scene->metadata.warnings.data[i].description)));

    // ---- Materials and their textures ----
    std::unordered_map<std::uint32_t, int> image_of;  // ufbx texture -> our image (embedded content)
    const auto texture_ref = [&](const ufbx_texture* t, const std::string& material) -> TextureRef {
        if (!t) return {};
        if (t->content.size > 0) {
            auto it = image_of.find(t->typed_id);
            if (it == image_of.end()) {
                ImportedImage img;
                img.name = text(t->name);
                const auto* bytes = static_cast<const std::uint8_t*>(t->content.data);
                img.bytes.assign(bytes, bytes + t->content.size);
                it = image_of.emplace(t->typed_id, static_cast<int>(model.images.size())).first;
                model.images.push_back(std::move(img));
            }
            return {path, it->second};
        }
        // External: the path the file names (resolved against it by ufbx), else the same name next to the FBX.
        for (const auto* name : {&t->filename, &t->relative_filename, &t->absolute_filename}) {
            if (name->length == 0) continue;
            const stdfs::path p(std::u8string(reinterpret_cast<const char8_t*>(name->data), name->length));
            for (const stdfs::path& candidate : {p, path.parent_path() / p, path.parent_path() / p.filename()})
                if (stdfs::exists(candidate, ec)) return {candidate, -1};
        }
        model.warnings.push_back(std::format("material '{}': texture '{}' isn't in the file or next to it", material,
                                             text(t->relative_filename.length ? t->relative_filename : t->filename)));
        return {};
    };
    std::unordered_map<std::uint32_t, int> material_of;  // ufbx material -> ours
    std::vector<Vec3> material_color;                    // their base colour factors (go into vertex colours)
    for (std::size_t i = 0; i < scene->materials.count; ++i) {
        const ufbx_material& src = *scene->materials.data[i];
        ImportedMaterial out;
        out.name = text(src.name);
        Material& m = out.material;
        const auto& pbr = src.pbr;
        Vec3 color{1, 1, 1};
        if (pbr.base_color.has_value) color = {static_cast<float>(pbr.base_color.value_vec4.x), static_cast<float>(pbr.base_color.value_vec4.y),
                                               static_cast<float>(pbr.base_color.value_vec4.z)};
        if (pbr.base_factor.has_value) color = color * static_cast<float>(pbr.base_factor.value_real);
        float alpha = 1.0f;
        if (pbr.opacity.has_value) alpha = static_cast<float>(std::clamp(pbr.opacity.value_real, 0.0, 1.0));
        m.base_color = {1, 1, 1, alpha};
        if (alpha < 0.999f) m.alpha = AlphaMode::Blend;
        if (pbr.roughness.has_value) m.roughness = static_cast<float>(std::clamp(pbr.roughness.value_real, 0.0, 1.0));
        if (pbr.metalness.has_value) m.metallic = static_cast<float>(std::clamp(pbr.metalness.value_real, 0.0, 1.0));
        m.base_color_map = texture_ref(pbr.base_color.texture_enabled || pbr.base_color.texture ? pbr.base_color.texture : nullptr, out.name);
        m.normal_map = texture_ref(pbr.normal_map.texture, out.name);
        if (pbr.emission_color.has_value) {
            const float f = src.pbr.emission_factor.has_value ? static_cast<float>(src.pbr.emission_factor.value_real) : 1.0f;
            m.emissive = Vec3{static_cast<float>(pbr.emission_color.value_vec4.x), static_cast<float>(pbr.emission_color.value_vec4.y),
                              static_cast<float>(pbr.emission_color.value_vec4.z)} * f;
        }
        material_of.emplace(src.typed_id, static_cast<int>(model.materials.size()));
        material_color.push_back(color);
        model.materials.push_back(std::move(out));
    }

    // ---- Meshes: triangulated, one submesh per material, vertices shared where every attribute agrees ----
    std::unordered_map<std::uint32_t, int> mesh_of;  // ufbx mesh -> ours
    std::vector<std::uint32_t> tri(64);
    for (std::size_t mi = 0; mi < scene->meshes.count; ++mi) {
        const ufbx_mesh& m = *scene->meshes.data[mi];
        ImportedMesh mesh;
        mesh.name = text(m.name);
        const std::size_t slots = std::max<std::size_t>(1, m.materials.count);
        std::vector<std::vector<std::uint32_t>> by_slot(slots);
        std::unordered_map<VertexKey, std::uint32_t, VertexKeyHash> shared;
        const ufbx_skin_deformer* skin = m.skin_deformers.count > 0 ? m.skin_deformers.data[0] : nullptr;
        SkinStats stats;
        if (tri.size() < m.max_face_triangles * 3) tri.resize(m.max_face_triangles * 3);
        const auto vertex_for = [&](std::uint32_t index, std::size_t slot) -> std::uint32_t {
            const std::uint32_t v = m.vertex_indices.data[index];
            const VertexKey key{{v, m.vertex_normal.exists ? m.vertex_normal.indices.data[index] : 0u, m.vertex_uv.exists ? m.vertex_uv.indices.data[index] : 0u,
                                 m.vertex_color.exists ? m.vertex_color.indices.data[index] : 0u}};
            // Colours are per slot (the material's factor is in them): vertices aren't shared between slots.
            VertexKey slotted = key;
            slotted.k[3] = key.k[3] * static_cast<std::uint32_t>(slots) + static_cast<std::uint32_t>(slot);
            const auto [it, added] = shared.emplace(slotted, static_cast<std::uint32_t>(mesh.vertices.size()));
            if (!added) return it->second;
            MeshVertex out;
            out.position = to_vec3(m.vertex_position.values.data[m.vertex_position.indices.data[index]]);
            out.normal = m.vertex_normal.exists ? math::normalize(to_vec3(m.vertex_normal.values.data[key.k[1]])) : Vec3{0, 1, 0};
            if (m.vertex_uv.exists) {
                const ufbx_vec2 uv = m.vertex_uv.values.data[key.k[2]];
                out.uv = {static_cast<float>(uv.x), static_cast<float>(1.0 - uv.y)};  // FBX's v runs up, glTF's down
            }
            Vec3 color{1, 1, 1};
            if (m.vertex_color.exists) {
                const ufbx_vec4 c = m.vertex_color.values.data[key.k[3]];
                color = {static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.z)};
            }
            if (slot < m.materials.count && m.materials.data[slot])
                if (const auto f = material_of.find(m.materials.data[slot]->typed_id); f != material_of.end())
                    color = {color.x * material_color[static_cast<std::size_t>(f->second)].x, color.y * material_color[static_cast<std::size_t>(f->second)].y,
                             color.z * material_color[static_cast<std::size_t>(f->second)].z};
            out.color = color;
            mesh.vertices.push_back(out);
            if (skin) {
                // The strongest four influences, renormalised.
                SkinWeights sw;
                sw.weights = {0, 0, 0, 0};
                std::array<std::pair<float, std::uint32_t>, 4> best{};
                std::size_t count = 0;
                float total = 0.0f;
                if (v < skin->vertices.count) {
                    const ufbx_skin_vertex& sv = skin->vertices.data[v];
                    for (std::uint32_t w = 0; w < sv.num_weights; ++w) {
                        const ufbx_skin_weight& sk = skin->weights.data[sv.weight_begin + w];
                        const auto weight = static_cast<float>(sk.weight);
                        if (!(weight > 0.0f)) continue;
                        ++count;
                        auto smallest = std::ranges::min_element(best, {}, &std::pair<float, std::uint32_t>::first);
                        if (weight > smallest->first) *smallest = {weight, sk.cluster_index};
                    }
                }
                if (count > 4) {
                    ++stats.over_four;
                    stats.dropped += count - 4;
                }
                std::ranges::sort(best, std::greater<>{}, &std::pair<float, std::uint32_t>::first);
                for (const auto& [w, c] : best) total += w;
                if (total <= 0.0f) {
                    ++stats.weightless;
                    sw.weights = {1, 0, 0, 0};
                } else {
                    if (std::abs(total - 1.0f) > 1e-3f) ++stats.renormalised;
                    for (std::size_t j = 0; j < 4; ++j) sw.joints[j] = static_cast<std::uint16_t>(best[j].first > 0.0f ? best[j].second : 0);
                    sw.weights = {best[0].first / total, best[1].first / total, best[2].first / total, best[3].first / total};
                }
                mesh.skin.push_back(sw);
            }
            return it->second;
        };
        for (std::size_t f = 0; f < m.faces.count; ++f) {
            const ufbx_face face = m.faces.data[f];
            if (face.num_indices < 3) continue;
            const std::size_t slot = m.face_material.count > f ? std::min<std::size_t>(m.face_material.data[f], slots - 1) : 0;
            const std::uint32_t n = ufbx_triangulate_face(tri.data(), tri.size(), &m, face);
            for (std::uint32_t t = 0; t < n * 3; ++t) by_slot[slot].push_back(vertex_for(tri[t], slot));
        }
        for (std::size_t s = 0; s < slots; ++s) {
            if (by_slot[s].empty()) continue;
            Submesh sub;
            sub.first_index = static_cast<std::uint32_t>(mesh.indices.size());
            sub.index_count = static_cast<std::uint32_t>(by_slot[s].size());
            if (s < m.materials.count && m.materials.data[s])
                if (const auto f = material_of.find(m.materials.data[s]->typed_id); f != material_of.end()) sub.material = f->second;
            mesh.indices.insert(mesh.indices.end(), by_slot[s].begin(), by_slot[s].end());
            mesh.submeshes.push_back(sub);
        }
        if (!mesh.vertices.empty()) compute_tangents(mesh.vertices, mesh.indices, 0, 0);
        if (stats.over_four > 0)
            model.warnings.push_back(std::format("mesh '{}': vertices with more than 4 joints: the strongest 4 are kept ({} lesser influences dropped)",
                                                 mesh.name, stats.dropped));
        if (stats.renormalised > 0)
            model.warnings.push_back(std::format("mesh '{}': {} vertices' skin weights didn't add up to 1; they were scaled so they do", mesh.name, stats.renormalised));
        if (stats.weightless > 0)
            model.warnings.push_back(std::format("mesh '{}': {} vertices have no skin weight; they follow the first joint", mesh.name, stats.weightless));
        mesh_of.emplace(m.typed_id, static_cast<int>(model.meshes.size()));
        model.meshes.push_back(std::move(mesh));
    }

    // ---- Nodes: depth first from the root, so parents come first (ufbx's own root isn't one of them) ----
    std::vector<int> ours(scene->nodes.count, -1);
    std::vector<const ufbx_node*> source_of;  // ours -> ufbx's
    struct Pending {
        const ufbx_node* node;
        int parent;
    };
    std::vector<Pending> stack;
    for (std::size_t c = scene->root_node->children.count; c-- > 0;) stack.push_back({scene->root_node->children.data[c], -1});
    while (!stack.empty()) {
        const Pending item = stack.back();
        stack.pop_back();
        const ufbx_node* n = item.node;
        if (n->typed_id >= ours.size() || ours[n->typed_id] >= 0) continue;  // (a node reached twice: once is enough)
        ImportedNode node;
        node.name = text(n->name);
        if (node.name.empty()) node.name = std::format("node_{}", n->typed_id);
        node.parent = item.parent;
        // The rest pose: the file's bind pose where it has one (what the skin was bound in; Blender writes the current
        // animated pose as the nodes' own transforms), else the node's own transform.
        ufbx_transform local = n->local_transform;
        if (n->bind_pose)
            for (std::size_t k = 0; k < n->bind_pose->bone_poses.count; ++k)
                if (n->bind_pose->bone_poses.data[k].bone_node == n) {
                    local = ufbx_matrix_to_transform(&n->bind_pose->bone_poses.data[k].bone_to_parent);
                    break;
                }
        node.translation = to_vec3(local.translation);
        node.rotation = math::normalize_quat(to_quat(local.rotation));
        node.scale = to_vec3(local.scale);
        if (n->mesh)
            if (const auto f = mesh_of.find(n->mesh->typed_id); f != mesh_of.end()) node.mesh = f->second;
        const int index = static_cast<int>(model.nodes.size());
        ours[n->typed_id] = index;
        source_of.push_back(n);
        model.nodes.push_back(std::move(node));
        for (std::size_t c = n->children.count; c-- > 0;) stack.push_back({n->children.data[c], index});
    }

    // A bone that drives a skin rests where its cluster says it was bound (Blender's importer does the same): a file's
    // bind pose can leave bones out, and then the stored pose isn't where the skin was bound. Parents first, so each
    // bone's local rest is taken against its parent's rest.
    {
        std::unordered_map<const ufbx_node*, ufbx_matrix> bound;
        for (std::size_t i = 0; i < scene->skin_deformers.count; ++i)
            for (std::size_t c = 0; c < scene->skin_deformers.data[i]->clusters.count; ++c)
                if (const ufbx_skin_cluster* cl = scene->skin_deformers.data[i]->clusters.data[c]; cl->bone_node)
                    bound.emplace(cl->bone_node, cl->bind_to_world);  // the first skin's, if two disagree
        std::vector<ufbx_matrix> world(model.nodes.size());
        for (std::size_t i = 0; i < model.nodes.size(); ++i) {
            ImportedNode& node = model.nodes[i];
            const ufbx_matrix parent = node.parent >= 0 ? world[static_cast<std::size_t>(node.parent)] : ufbx_identity_matrix;
            if (const auto b = bound.find(source_of[i]); b != bound.end()) {
                world[i] = b->second;
                const ufbx_matrix inv = ufbx_matrix_invert(&parent);
                const ufbx_matrix local_m = ufbx_matrix_mul(&inv, &world[i]);
                const ufbx_transform local = ufbx_matrix_to_transform(&local_m);
                node.translation = to_vec3(local.translation);
                node.rotation = math::normalize_quat(to_quat(local.rotation));
                node.scale = to_vec3(local.scale);
            } else {
                ufbx_transform t{};
                t.translation = {node.translation.x, node.translation.y, node.translation.z};
                t.rotation = {node.rotation.x, node.rotation.y, node.rotation.z, node.rotation.w};
                t.scale = {node.scale.x, node.scale.y, node.scale.z};
                const ufbx_matrix local_m = ufbx_transform_to_matrix(&t);
                world[i] = ufbx_matrix_mul(&parent, &local_m);
            }
        }
    }

    // ---- Skins: one per skin deformer; a node drawing its mesh is skinned by it ----
    std::unordered_map<std::uint32_t, int> skin_of;  // ufbx skin deformer -> ours (-1: unusable)
    for (std::size_t i = 0; i < scene->skin_deformers.count; ++i) {
        const ufbx_skin_deformer& d = *scene->skin_deformers.data[i];
        ImportedSkin out;
        out.name = text(d.name);
        bool ok = d.clusters.count > 0;
        if (d.clusters.count > kMaxSkinJoints) {
            model.warnings.push_back(std::format("skin '{}' has {} joints, more than the {} a skin can have: drawn unskinned", out.name, d.clusters.count, kMaxSkinJoints));
            ok = false;
        }
        for (std::size_t c = 0; ok && c < d.clusters.count; ++c) {
            const ufbx_skin_cluster& cl = *d.clusters.data[c];
            const int node = cl.bone_node && cl.bone_node->typed_id < ours.size() ? ours[cl.bone_node->typed_id] : -1;
            if (node < 0) {
                model.warnings.push_back(std::format("skin '{}': cluster '{}' has no bone in the scene: drawn unskinned", out.name, text(cl.name)));
                ok = false;
                break;
            }
            out.joints.push_back(node);
            out.inverse_bind.push_back(to_mat4(cl.geometry_to_bone));
        }
        skin_of.emplace(d.typed_id, ok ? static_cast<int>(model.skins.size()) : -1);
        if (ok) model.skins.push_back(std::move(out));
    }
    for (std::size_t i = 0; i < scene->nodes.count; ++i) {
        const ufbx_node* n = scene->nodes.data[i];
        if (!n->mesh || n->mesh->skin_deformers.count == 0 || ours[n->typed_id] < 0) continue;
        const auto s = skin_of.find(n->mesh->skin_deformers.data[0]->typed_id);
        const int mesh = model.nodes[static_cast<std::size_t>(ours[n->typed_id])].mesh;
        if (s == skin_of.end() || s->second < 0 || mesh < 0 || model.meshes[static_cast<std::size_t>(mesh)].skin.empty()) continue;
        model.nodes[static_cast<std::size_t>(ours[n->typed_id])].skin = s->second;
    }
    for (std::size_t i = 0; i < scene->meshes.count; ++i)
        if (scene->meshes.data[i]->skin_deformers.count > 1) {
            model.warnings.push_back(std::format("mesh '{}' has more than one skin deformer: only the first is used", text(scene->meshes.data[i]->name)));
            break;
        }
    if (scene->blend_deformers.count > 0) model.warnings.push_back("blend shapes (morph targets) aren't imported");

    // ---- Animation: each take baked into linear keys at 60 a second ----
    std::unordered_set<std::string> names;
    for (std::size_t a = 0; a < scene->anim_stacks.count; ++a) {
        const ufbx_anim_stack& stack_ = *scene->anim_stacks.data[a];
        ufbx_bake_opts bake{};
        bake.resample_rate = 60.0;
        bake.trim_start_time = true;
        ufbx_error bake_error{};
        std::unique_ptr<ufbx_baked_anim, BakeDeleter> baked(ufbx_bake_anim(scene.get(), stack_.anim, &bake, &bake_error));
        if (!baked) {
            model.warnings.push_back(std::format("take '{}' couldn't be baked: {}", text(stack_.name), describe(bake_error)));
            continue;
        }
        // What this take animates (per element: translation, rotation, scale). A node it doesn't animate stays in its
        // rest pose, as in glTF; ufbx's baker would hand back its stored transform (Blender writes the pose it happened
        // to be in there).
        std::unordered_map<std::uint32_t, std::array<bool, 3>> animated;
        for (std::size_t l = 0; l < stack_.layers.count; ++l) {
            const ufbx_anim_layer& layer = *stack_.layers.data[l];
            for (std::size_t k = 0; k < layer.anim_props.count; ++k) {
                const ufbx_anim_prop& ap = layer.anim_props.data[k];
                if (!ap.element) continue;
                const std::string_view prop(ap.prop_name.data, ap.prop_name.length);
                const int which = prop == "Lcl Translation" ? 0 : prop == "Lcl Rotation" ? 1 : prop == "Lcl Scaling" ? 2 : -1;
                if (which >= 0) animated[ap.element->element_id][static_cast<std::size_t>(which)] = true;
            }
        }
        ImportedAnimation clip;
        clip.name = clip_name(text(stack_.name), a);
        for (int n = 2; !names.insert(clip.name).second; ++n) clip.name = std::format("{} {}", clip_name(text(stack_.name), a), n);
        for (std::size_t b = 0; b < baked->nodes.count; ++b) {
            const ufbx_baked_node& bn = baked->nodes.data[b];
            if (bn.typed_id >= ours.size() || ours[bn.typed_id] < 0) continue;
            const int node = ours[bn.typed_id];
            const ImportedNode& rest = model.nodes[static_cast<std::size_t>(node)];
            const auto moved = animated.find(bn.element_id);
            // Helper nodes ufbx adds (geometry transforms, scale compensation) follow their nodes: kept when they move.
            const bool helper = scene->nodes.data[bn.typed_id]->is_geometry_transform_helper || scene->nodes.data[bn.typed_id]->is_scale_helper;
            const auto keep = [&](int which, bool constant) {
                if (helper) return true;
                if (moved != animated.end() && moved->second[static_cast<std::size_t>(which)]) return true;
                return !constant;  // not animated by its own curves yet moving (inherited through a helper): keep
            };
            const auto vec_channel = [&](const ufbx_baked_vec3_list& keys, bool constant, AnimationChannel::Path p, Vec3 at_rest) {
                if (keys.count == 0) return;
                if (constant) {  // the same all the way: a channel only if it isn't the rest pose
                    const Vec3 v = to_vec3(keys.data[0].value);
                    if (std::abs(v.x - at_rest.x) < 1e-6f && std::abs(v.y - at_rest.y) < 1e-6f && std::abs(v.z - at_rest.z) < 1e-6f) return;
                }
                AnimationChannel ch;
                ch.node = node;
                ch.path = p;
                for (std::size_t k = 0; k < keys.count; ++k) {
                    ch.times.push_back(static_cast<float>(keys.data[k].time));
                    const Vec3 v = to_vec3(keys.data[k].value);
                    ch.values.insert(ch.values.end(), {v.x, v.y, v.z});
                }
                clip.duration = std::max(clip.duration, ch.times.back());
                clip.channels.push_back(std::move(ch));
            };
            if (keep(0, bn.constant_translation)) vec_channel(bn.translation_keys, bn.constant_translation, AnimationChannel::Path::Translation, rest.translation);
            if (keep(2, bn.constant_scale)) vec_channel(bn.scale_keys, bn.constant_scale, AnimationChannel::Path::Scale, rest.scale);
            if (bn.rotation_keys.count > 0 && keep(1, bn.constant_rotation)) {
                const math::Quat first = to_quat(bn.rotation_keys.data[0].value);
                const math::Quat r = rest.rotation;
                const bool still = bn.constant_rotation && std::abs(std::abs(first.x * r.x + first.y * r.y + first.z * r.z + first.w * r.w) - 1.0f) < 1e-6f;
                if (!still) {
                    AnimationChannel ch;
                    ch.node = node;
                    ch.path = AnimationChannel::Path::Rotation;
                    math::Quat prev{};
                    for (std::size_t k = 0; k < bn.rotation_keys.count; ++k) {
                        math::Quat q = math::normalize_quat(to_quat(bn.rotation_keys.data[k].value));
                        if (k > 0 && q.x * prev.x + q.y * prev.y + q.z * prev.z + q.w * prev.w < 0.0f) q = {-q.x, -q.y, -q.z, -q.w};  // one hemisphere
                        prev = q;
                        ch.times.push_back(static_cast<float>(bn.rotation_keys.data[k].time));
                        ch.values.insert(ch.values.end(), {q.x, q.y, q.z, q.w});
                    }
                    clip.duration = std::max(clip.duration, ch.times.back());
                    clip.channels.push_back(std::move(ch));
                }
            }
        }
        if (clip.duration <= 0.0f) clip.duration = static_cast<float>(std::max(0.0, baked->playback_duration));
        // The take's span is the clip (as FBX SDK tools and Unreal play it); keys outside it aren't played: said.
        if (baked->key_time_max > baked->playback_time_end + 0.02 || baked->key_time_min < baked->playback_time_begin - 0.02)
            model.warnings.push_back(std::format("take '{}' runs {:.2f} to {:.2f} s; its curves have keys from {:.2f} to {:.2f} s (outside the take: not played)",
                                                 text(stack_.name), baked->playback_time_begin, baked->playback_time_end, baked->key_time_min,
                                                 baked->key_time_max));
        model.animations.push_back(std::move(clip));
    }
    return model;
}

} // namespace noor::asset
