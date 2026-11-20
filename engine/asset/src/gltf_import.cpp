#include "noor/asset/gltf_import.h"
#include "mesh_util.h"

#include "noor/foundation/log.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <memory>

namespace noor::asset {
namespace {
namespace stdfs = std::filesystem;
using math::Vec3;

struct CgltfDeleter {
    void operator()(cgltf_data* d) const { cgltf_free(d); }
};

const char* result_name(cgltf_result r) {
    switch (r) {
    case cgltf_result_success: return "success";
    case cgltf_result_data_too_short: return "data too short";
    case cgltf_result_unknown_format: return "unknown format";
    case cgltf_result_invalid_json: return "invalid JSON";
    case cgltf_result_invalid_gltf: return "invalid glTF";
    case cgltf_result_invalid_options: return "invalid options";
    case cgltf_result_file_not_found: return "file not found";
    case cgltf_result_io_error: return "I/O error";
    case cgltf_result_out_of_memory: return "out of memory";
    case cgltf_result_legacy_gltf: return "legacy glTF 1.0";
    default: return "unknown error";
    }
}

// Decomposes an affine matrix (glTF column-major float[16]) into TRS.
void decompose(const float* m, Vec3& t, math::Quat& r, Vec3& s) {
    t = {m[12], m[13], m[14]};
    Vec3 c0{m[0], m[1], m[2]}, c1{m[4], m[5], m[6]}, c2{m[8], m[9], m[10]};
    s = {math::length(c0), math::length(c1), math::length(c2)};
    if (s.x > 0) c0 = c0 * (1.0f / s.x);
    if (s.y > 0) c1 = c1 * (1.0f / s.y);
    if (s.z > 0) c2 = c2 * (1.0f / s.z);
    const float trace = c0.x + c1.y + c2.z;
    if (trace > 0.0f) {
        const float k = 0.5f / std::sqrt(trace + 1.0f);
        r = {(c1.z - c2.y) * k, (c2.x - c0.z) * k, (c0.y - c1.x) * k, 0.25f / k};
    } else if (c0.x > c1.y && c0.x > c2.z) {
        const float k = 2.0f * std::sqrt(1.0f + c0.x - c1.y - c2.z);
        r = {0.25f * k, (c1.x + c0.y) / k, (c2.x + c0.z) / k, (c1.z - c2.y) / k};
    } else if (c1.y > c2.z) {
        const float k = 2.0f * std::sqrt(1.0f + c1.y - c0.x - c2.z);
        r = {(c1.x + c0.y) / k, 0.25f * k, (c2.y + c1.z) / k, (c2.x - c0.z) / k};
    } else {
        const float k = 2.0f * std::sqrt(1.0f + c2.z - c0.x - c1.y);
        r = {(c2.x + c0.z) / k, (c2.y + c1.z) / k, 0.25f * k, (c0.y - c1.x) / k};
    }
}

Json parse_noor_extras(const cgltf_extras& extras, std::vector<std::string>& warnings, std::string_view node_name) {
    if (!extras.data) return nullptr;
    auto json = parse_json(extras.data, std::format("extras of node '{}'", node_name));
    if (!json) {
        warnings.push_back(json.error().message);
        return nullptr;
    }
    if (!json->is_object() || !json->contains("noor")) return nullptr;
    Json noor = (*json)["noor"];
    if (!noor.is_object()) {
        warnings.push_back(std::format("node '{}': extras.noor is not an object; ignored", node_name));
        return nullptr;
    }
    return noor;
}

// Appends one triangle primitive to `out`. Returns false (with a warning) if it can't be used.
// What skin reading had to fix in a mesh, reported once per mesh in plain words.
struct SkinStats {
    std::size_t over_four_sets = 0, dropped = 0, renormalised = 0, weightless = 0;
};

bool append_primitive(const cgltf_primitive& prim, ImportedMesh& out, std::vector<std::string>& warnings,
                      const cgltf_material* data_materials, SkinStats& skin_stats) {
    if (prim.type != cgltf_primitive_type_triangles) {
        warnings.push_back(std::format("mesh '{}': skipped a non-triangle primitive", out.name));
        return false;
    }
    const cgltf_accessor* pos = nullptr;
    const cgltf_accessor* nrm = nullptr;
    const cgltf_accessor* col = nullptr;
    const cgltf_accessor* tex = nullptr;
    const cgltf_accessor* tan = nullptr;
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        const auto& a = prim.attributes[i];
        if (a.type == cgltf_attribute_type_position && a.index == 0) pos = a.data;
        if (a.type == cgltf_attribute_type_normal && a.index == 0) nrm = a.data;
        if (a.type == cgltf_attribute_type_color && a.index == 0) col = a.data;
        if (a.type == cgltf_attribute_type_texcoord && a.index == 0) tex = a.data;
        if (a.type == cgltf_attribute_type_tangent && a.index == 0) tan = a.data;
    }
    if (!pos || pos->count == 0) {
        warnings.push_back(std::format("mesh '{}': primitive without positions skipped", out.name));
        return false;
    }
    if (tex && tex->count != pos->count) tex = nullptr;  // broken optional data: ignore it, keep the mesh
    if (tan && tan->count != pos->count) tan = nullptr;
    if ((nrm && nrm->count != pos->count) || (col && col->count != pos->count)) {
        warnings.push_back(std::format("mesh '{}': attribute counts disagree; primitive skipped", out.name));
        return false;
    }

    Vec3 base{1, 1, 1};
    if (prim.material && prim.material->has_pbr_metallic_roughness) {
        const float* f = prim.material->pbr_metallic_roughness.base_color_factor;
        base = {f[0], f[1], f[2]};  // glTF factors are linear already
    }

    const auto first = static_cast<std::uint32_t>(out.vertices.size());
    const std::size_t count = pos->count;
    if (out.vertices.size() + count > std::numeric_limits<std::uint32_t>::max()) {
        warnings.push_back(std::format("mesh '{}': too many vertices; primitive skipped", out.name));
        return false;
    }
    out.vertices.reserve(out.vertices.size() + count);
    for (cgltf_size v = 0; v < count; ++v) {
        MeshVertex vert;
        float tmp[4] = {0, 0, 0, 1};
        cgltf_accessor_read_float(pos, v, tmp, 3);
        vert.position = {tmp[0], tmp[1], tmp[2]};
        if (nrm) {
            cgltf_accessor_read_float(nrm, v, tmp, 3);
            vert.normal = {tmp[0], tmp[1], tmp[2]};
        }
        vert.color = base;
        if (col) {
            float c[4] = {1, 1, 1, 1};
            cgltf_accessor_read_float(col, v, c, cgltf_num_components(col->type));
            vert.color = {base.x * c[0], base.y * c[1], base.z * c[2]};
        }
        if (tex) {
            float t[2] = {0, 0};
            cgltf_accessor_read_float(tex, v, t, 2);
            vert.uv = {t[0], t[1]};
        }
        if (tan) {
            float t[4] = {1, 0, 0, 1};
            cgltf_accessor_read_float(tan, v, t, 4);
            vert.tangent = {t[0], t[1], t[2], t[3] < 0 ? -1.0f : 1.0f};
        }
        out.vertices.push_back(vert);
    }

    // Skins (8C): JOINTS_0/WEIGHTS_0, and _1 when a vertex has more than four influences: the four strongest are kept.
    const cgltf_accessor* joints[2] = {nullptr, nullptr};
    const cgltf_accessor* weights[2] = {nullptr, nullptr};
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        const auto& a = prim.attributes[i];
        if (a.index > 1) continue;
        if (a.type == cgltf_attribute_type_joints && a.data && a.data->count == count) joints[a.index] = a.data;
        if (a.type == cgltf_attribute_type_weights && a.data && a.data->count == count) weights[a.index] = a.data;
    }
    const bool skinned = joints[0] && weights[0];
    if (skinned || !out.skin.empty()) {
        if (!skinned) warnings.push_back(std::format("mesh '{}': some primitives have no skin weights; they follow the first joint", out.name));
        if (joints[1] && weights[1]) ++skin_stats.over_four_sets;
        out.skin.resize(first);  // earlier unskinned primitives of this mesh: the first joint
        for (cgltf_size v = 0; v < count; ++v) {
            std::array<std::pair<float, std::uint32_t>, 8> all{};
            int n = 0;
            for (int set = 0; set < 2 && skinned; ++set) {
                if (!joints[set] || !weights[set]) continue;
                cgltf_uint j[4] = {0, 0, 0, 0};
                float w[4] = {0, 0, 0, 0};
                cgltf_accessor_read_uint(joints[set], v, j, 4);
                cgltf_accessor_read_float(weights[set], v, w, 4);
                for (int k = 0; k < 4; ++k) all[static_cast<std::size_t>(n++)] = {std::max(0.0f, w[k]), j[k]};
            }
            std::stable_sort(all.begin(), all.begin() + n, [](const auto& a, const auto& b) { return a.first > b.first; });
            SkinWeights sw;
            float sum = 0.0f;
            for (int k = 0; k < 4 && k < n; ++k) sum += all[static_cast<std::size_t>(k)].first;
            for (int k = n > 4 ? 4 : n; k < n; ++k)
                if (all[static_cast<std::size_t>(k)].first > 0.0f) ++skin_stats.dropped;
            if (n == 0 || sum <= 1e-8f) {
                if (skinned) ++skin_stats.weightless;
                sw.weights = {1, 0, 0, 0};
            } else {
                if (std::abs(sum - 1.0f) > 1e-3f) ++skin_stats.renormalised;
                float* wv = &sw.weights.x;
                for (int k = 0; k < 4; ++k) {
                    const bool used = k < n;
                    sw.joints[static_cast<std::size_t>(k)] = used ? static_cast<std::uint16_t>(std::min<std::uint32_t>(all[static_cast<std::size_t>(k)].second, 0xffffu)) : 0;
                    wv[k] = used ? all[static_cast<std::size_t>(k)].first / sum : 0.0f;
                }
            }
            out.skin.push_back(sw);
        }
    }

    const std::size_t index_start = out.indices.size();
    if (prim.indices) {
        if (prim.indices->count % 3 != 0) {
            warnings.push_back(std::format("mesh '{}': index count not a multiple of 3; primitive skipped", out.name));
            out.vertices.resize(first);
            if (!out.skin.empty()) out.skin.resize(first);
            return false;
        }
        out.indices.reserve(index_start + prim.indices->count);
        for (cgltf_size i = 0; i < prim.indices->count; ++i) {
            const cgltf_size idx = cgltf_accessor_read_index(prim.indices, i);
            if (idx >= count) {
                warnings.push_back(std::format("mesh '{}': index {} out of range; primitive skipped", out.name, idx));
                out.vertices.resize(first);
                if (!out.skin.empty()) out.skin.resize(first);
                out.indices.resize(index_start);
                return false;
            }
            out.indices.push_back(first + static_cast<std::uint32_t>(idx));
        }
    } else {
        for (std::size_t i = 0; i + 2 < count; i += 3) {
            for (std::uint32_t k = 0; k < 3; ++k) out.indices.push_back(first + static_cast<std::uint32_t>(i) + k);
        }
    }

    if (!nrm) {  // flat-ish normals: accumulate face normals per vertex
        for (std::size_t i = index_start; i + 2 < out.indices.size(); i += 3) {
            MeshVertex& a = out.vertices[out.indices[i]];
            MeshVertex& b = out.vertices[out.indices[i + 1]];
            MeshVertex& c = out.vertices[out.indices[i + 2]];
            const Vec3 n = math::cross(b.position - a.position, c.position - a.position);
            a.normal = a.normal + n;
            b.normal = b.normal + n;
            c.normal = c.normal + n;
        }
        for (std::size_t v = first; v < out.vertices.size(); ++v) {
            out.vertices[v].normal = math::normalize(out.vertices[v].normal);
        }
    }
    if (!tan) compute_tangents(out.vertices, out.indices, first, index_start);
    out.submeshes.push_back({static_cast<std::uint32_t>(index_start), static_cast<std::uint32_t>(out.indices.size() - index_start),
                             prim.material ? static_cast<int>(prim.material - data_materials) : -1});
    return true;
}

} // namespace

// Tangents from the UVs when the file has none (per-triangle directions, summed per vertex,
// made perpendicular to the normal). glTF asks for MikkTSpace; this simpler method agrees with it
// on ordinary meshes and is replaced by MikkTSpace if normal maps show seams (Milestone 5, part 3).
void compute_tangents(std::vector<MeshVertex>& vertices, const std::vector<std::uint32_t>& indices,
                      std::size_t first_vertex, std::size_t first_index) {
    std::vector<Vec3> tangents(vertices.size() - first_vertex, Vec3{0, 0, 0});
    std::vector<Vec3> bitangents(tangents.size(), Vec3{0, 0, 0});
    for (std::size_t i = first_index; i + 2 < indices.size(); i += 3) {
        const std::uint32_t ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
        const MeshVertex& a = vertices[ia];
        const MeshVertex& b = vertices[ib];
        const MeshVertex& c = vertices[ic];
        const Vec3 e1 = b.position - a.position, e2 = c.position - a.position;
        const float du1 = b.uv.x - a.uv.x, dv1 = b.uv.y - a.uv.y, du2 = c.uv.x - a.uv.x, dv2 = c.uv.y - a.uv.y;
        const float det = du1 * dv2 - du2 * dv1;
        if (std::abs(det) < 1e-12f) continue;  // no UVs, or a degenerate UV triangle
        const float r = 1.0f / det;
        const Vec3 t = (e1 * dv2 - e2 * dv1) * r;
        const Vec3 bt = (e2 * du1 - e1 * du2) * r;
        for (const std::uint32_t k : {ia, ib, ic}) {
            tangents[k - first_vertex] = tangents[k - first_vertex] + t;
            bitangents[k - first_vertex] = bitangents[k - first_vertex] + bt;
        }
    }
    for (std::size_t v = 0; v < tangents.size(); ++v) {
        MeshVertex& vert = vertices[first_vertex + v];
        const Vec3 n = vert.normal;
        Vec3 t = tangents[v] - n * math::dot(n, tangents[v]);  // Gram-Schmidt
        if (math::length(t) < 1e-8f) {  // no UV direction here: any vector perpendicular to the normal
            t = math::cross(std::abs(n.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}, n);
        }
        t = math::normalize(t);
        const float w = math::dot(math::cross(n, t), bitangents[v]) < 0.0f ? -1.0f : 1.0f;
        vert.tangent = {t.x, t.y, t.z, w};
    }
}


math::Mat4 ImportedModel::world_matrix(std::size_t i) const {
    math::Mat4 m = math::compose(nodes[i].translation, nodes[i].rotation, nodes[i].scale);
    for (int p = nodes[i].parent; p >= 0; p = nodes[static_cast<std::size_t>(p)].parent) {
        const auto& n = nodes[static_cast<std::size_t>(p)];
        m = math::compose(n.translation, n.rotation, n.scale) * m;
    }
    return m;
}

std::uint64_t ImportedModel::render_triangles() const {
    std::uint64_t total = 0;
    for (const auto& n : nodes) {
        if (n.mesh >= 0 && !n.is_collision()) total += meshes[static_cast<std::size_t>(n.mesh)].triangles();
    }
    return total;
}

std::uint64_t ImportedModel::collision_triangles() const {
    std::uint64_t total = 0;
    for (const auto& n : nodes) {
        if (n.mesh >= 0 && n.is_collision()) total += meshes[static_cast<std::size_t>(n.mesh)].triangles();
    }
    return total;
}

Result<ImportedModel> import_gltf(const stdfs::path& path, std::uint64_t max_file_bytes) {
    std::error_code ec;
    const auto size = stdfs::file_size(path, ec);
    if (ec) return fail(ErrorCode::NotFound, std::format("Cannot read '{}': {}", path.generic_string(), ec.message()));
    if (size > max_file_bytes) {
        return fail(ErrorCode::InvalidArgument, std::format("'{}' is {} bytes, above the import limit of {}",
                                                            path.generic_string(), size, max_file_bytes));
    }

    const std::string file = path.string();
    cgltf_options options{};
    cgltf_data* raw = nullptr;
    cgltf_result r = cgltf_parse_file(&options, file.c_str(), &raw);
    std::unique_ptr<cgltf_data, CgltfDeleter> data(raw);
    if (r == cgltf_result_success) r = cgltf_load_buffers(&options, data.get(), file.c_str());
    if (r == cgltf_result_success) r = cgltf_validate(data.get());
    if (r != cgltf_result_success) {
        return fail(ErrorCode::ParseError, std::format("'{}': {}", path.generic_string(), result_name(r)));
    }

    ImportedModel model;
    // Images: the bytes of embedded (buffer view) images; external files are not supported yet.
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        const cgltf_image& img = data->images[i];
        ImportedImage out;
        out.name = img.name ? img.name : std::format("image_{}", i);
        if (img.buffer_view && img.buffer_view->buffer && img.buffer_view->buffer->data &&
            img.buffer_view->offset + img.buffer_view->size <= img.buffer_view->buffer->size) {
            const auto* bytes = static_cast<const std::uint8_t*>(img.buffer_view->buffer->data) + img.buffer_view->offset;
            out.bytes.assign(bytes, bytes + img.buffer_view->size);
        } else {
            model.warnings.push_back(std::format("image '{}': only images inside the GLB are supported", out.name));
        }
        model.images.push_back(std::move(out));
    }
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material& src = data->materials[i];
        ImportedMaterial out;
        out.name = src.name ? src.name : std::format("material_{}", i);
        Material& m = out.material;
        const auto ref = [&](const cgltf_texture_view& view) -> TextureRef {
            if (!view.texture || !view.texture->image) return {};
            const auto index = static_cast<int>(cgltf_image_index(data.get(), view.texture->image));
            if (index < 0 || index >= static_cast<int>(model.images.size()) || model.images[static_cast<std::size_t>(index)].bytes.empty()) return {};
            if (view.texcoord != 0) model.warnings.push_back(std::format("material '{}': only TEXCOORD_0 is used", out.name));
            return {path, index};
        };
        if (src.has_pbr_metallic_roughness) {
            const auto& pbr = src.pbr_metallic_roughness;
            m.base_color = {1, 1, 1, pbr.base_color_factor[3]};  // rgb is in the vertex colours
            m.metallic = pbr.metallic_factor;
            m.roughness = pbr.roughness_factor;
            m.base_color_map = ref(pbr.base_color_texture);
            m.metallic_roughness_map = ref(pbr.metallic_roughness_texture);
        }
        m.normal_map = ref(src.normal_texture);
        m.normal_scale = src.normal_texture.texture ? src.normal_texture.scale : 1.0f;
        m.occlusion_map = ref(src.occlusion_texture);
        m.occlusion_strength = src.occlusion_texture.texture ? src.occlusion_texture.scale : 1.0f;
        m.emissive = {src.emissive_factor[0], src.emissive_factor[1], src.emissive_factor[2]};
        m.emissive_map = ref(src.emissive_texture);
        if (src.has_emissive_strength) m.emissive_strength = src.emissive_strength.emissive_strength;
        m.alpha = src.alpha_mode == cgltf_alpha_mode_mask ? AlphaMode::Cutout
                  : src.alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Blend
                                                              : AlphaMode::Opaque;
        m.alpha_cutoff = src.alpha_cutoff;
        m.double_sided = src.double_sided;
        model.materials.push_back(std::move(out));
    }
    model.meshes.reserve(data->meshes_count);
    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        ImportedMesh mesh;
        mesh.name = data->meshes[m].name ? data->meshes[m].name : std::format("mesh_{}", m);
        SkinStats stats;
        for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p) {
            append_primitive(data->meshes[m].primitives[p], mesh, model.warnings, data->materials, stats);
        }
        if (stats.over_four_sets > 0)
            model.warnings.push_back(std::format("mesh '{}': vertices with more than 4 joints: the strongest 4 are kept ({} lesser influences dropped)",
                                                 mesh.name, stats.dropped));
        if (stats.renormalised > 0)
            model.warnings.push_back(std::format("mesh '{}': {} vertices' skin weights didn't add up to 1; they were scaled so they do", mesh.name, stats.renormalised));
        if (stats.weightless > 0)
            model.warnings.push_back(std::format("mesh '{}': {} vertices have no skin weight; they follow the first joint", mesh.name, stats.weightless));
        model.meshes.push_back(std::move(mesh));
    }

    // Nodes of the default scene in depth-first order, so parents come first.
    const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
    if (!scene) return fail(ErrorCode::ParseError, std::format("'{}' contains no scene", path.generic_string()));

    struct Pending {
        const cgltf_node* node;
        int parent;
    };
    std::vector<Pending> stack;
    std::vector<int> ours(data->nodes_count, -1);  // glTF node index -> index in model.nodes (skins name glTF's)
    std::vector<const cgltf_node*> skinned_nodes;
    for (cgltf_size i = scene->nodes_count; i-- > 0;) stack.push_back({scene->nodes[i], -1});
    std::size_t guard = 0;
    while (!stack.empty()) {
        if (++guard > 1'000'000) return fail(ErrorCode::ParseError, "Node hierarchy too large or cyclic");
        const Pending item = stack.back();
        stack.pop_back();
        const cgltf_node* n = item.node;

        ImportedNode node;
        node.name = n->name ? n->name : std::format("node_{}", cgltf_node_index(data.get(), n));
        node.parent = item.parent;
        if (n->has_matrix) {
            decompose(n->matrix, node.translation, node.rotation, node.scale);
        } else {
            if (n->has_translation) node.translation = {n->translation[0], n->translation[1], n->translation[2]};
            if (n->has_rotation) node.rotation = {n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]};
            if (n->has_scale) node.scale = {n->scale[0], n->scale[1], n->scale[2]};
        }
        if (n->mesh) node.mesh = static_cast<int>(cgltf_mesh_index(data.get(), n->mesh));
        node.noor = parse_noor_extras(n->extras, model.warnings, node.name);

        const int index = static_cast<int>(model.nodes.size());
        ours[cgltf_node_index(data.get(), n)] = index;
        if (n->skin && n->mesh) skinned_nodes.push_back(n);
        model.nodes.push_back(std::move(node));
        for (cgltf_size c = n->children_count; c-- > 0;) stack.push_back({n->children[c], index});
    }

    // Skins (8C): joints mapped to our nodes; a skin whose joints aren't all in the scene, or that has more than the
    // renderer's 256 joints, isn't used (its meshes are drawn as they were bound, rigid).
    std::vector<int> skin_index(data->skins_count, -2);  // -2: not looked at yet, -1: unusable
    const auto use_skin = [&](const cgltf_skin* s) -> int {
        const auto i = cgltf_skin_index(data.get(), s);
        if (skin_index[i] != -2) return skin_index[i];
        ImportedSkin out;
        out.name = s->name ? s->name : std::format("skin_{}", i);
        bool ok = s->joints_count > 0;
        if (s->joints_count > kMaxSkinJoints) {
            model.warnings.push_back(std::format("skin '{}' has {} joints, more than the {} a skin can have: drawn unskinned", out.name, s->joints_count, kMaxSkinJoints));
            ok = false;
        }
        for (cgltf_size j = 0; ok && j < s->joints_count; ++j) {
            const int node = ours[cgltf_node_index(data.get(), s->joints[j])];
            if (node < 0) {
                model.warnings.push_back(std::format("skin '{}': joint '{}' isn't in the scene: drawn unskinned", out.name,
                                                     s->joints[j]->name ? s->joints[j]->name : "?"));
                ok = false;
                break;
            }
            out.joints.push_back(node);
            math::Mat4 ibm = math::Mat4::identity();
            if (s->inverse_bind_matrices) {
                float m[16];
                cgltf_accessor_read_float(s->inverse_bind_matrices, j, m, 16);
                for (int c = 0; c < 4; ++c) ibm.m[c] = {m[c * 4 + 0], m[c * 4 + 1], m[c * 4 + 2], m[c * 4 + 3]};  // column-major, as ours
            }
            out.inverse_bind.push_back(ibm);
        }
        if (!ok) return skin_index[i] = -1;
        skin_index[i] = static_cast<int>(model.skins.size());
        model.skins.push_back(std::move(out));
        return skin_index[i];
    };
    for (const cgltf_node* n : skinned_nodes) {
        const int skin = use_skin(n->skin);
        const int node = ours[cgltf_node_index(data.get(), n)];
        if (skin < 0 || node < 0) continue;
        auto& mesh = model.meshes[static_cast<std::size_t>(model.nodes[static_cast<std::size_t>(node)].mesh)];
        if (mesh.skin.empty()) continue;  // no weights: a rigid mesh after all
        const auto joints = model.skins[static_cast<std::size_t>(skin)].joints.size();
        std::size_t bad = 0;
        for (auto& sw : mesh.skin)
            for (auto& j : sw.joints)
                if (j >= joints) {
                    j = 0;
                    ++bad;
                }
        if (bad > 0) model.warnings.push_back(std::format("mesh '{}': {} skin influences name joints the skin doesn't have; they follow the first", mesh.name, bad));
        model.nodes[static_cast<std::size_t>(node)].skin = skin;
    }


    // Animations (8C part 2): translation, rotation and scale channels of nodes in the scene. Morph-target weights
    // aren't supported yet: those channels are skipped (said once).
    std::size_t morph_channels = 0;
    for (cgltf_size a = 0; a < data->animations_count; ++a) {
        const cgltf_animation& src = data->animations[a];
        ImportedAnimation clip;
        clip.name = src.name ? src.name : std::format("animation_{}", a);
        for (cgltf_size c = 0; c < src.channels_count; ++c) {
            const cgltf_animation_channel& ch = src.channels[c];
            if (ch.target_path == cgltf_animation_path_type_weights) {
                ++morph_channels;
                continue;
            }
            if (!ch.sampler || !ch.sampler->input || !ch.sampler->output || !ch.target_node) continue;
            const int node = ours[cgltf_node_index(data.get(), ch.target_node)];
            if (node < 0) continue;  // a node outside the scene
            AnimationChannel out;
            out.node = node;
            out.path = ch.target_path == cgltf_animation_path_type_rotation ? AnimationChannel::Path::Rotation
                       : ch.target_path == cgltf_animation_path_type_scale  ? AnimationChannel::Path::Scale
                                                                            : AnimationChannel::Path::Translation;
            out.interpolation = ch.sampler->interpolation == cgltf_interpolation_type_step           ? AnimationChannel::Interpolation::Step
                                : ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline ? AnimationChannel::Interpolation::CubicSpline
                                                                                                     : AnimationChannel::Interpolation::Linear;
            const cgltf_size keys = ch.sampler->input->count;
            const cgltf_size n = out.path == AnimationChannel::Path::Rotation ? 4 : 3;
            const cgltf_size per_key = out.interpolation == AnimationChannel::Interpolation::CubicSpline ? 3 : 1;
            if (keys == 0 || ch.sampler->output->count != keys * per_key || cgltf_num_components(ch.sampler->output->type) != n) {
                model.warnings.push_back(std::format("animation '{}': a channel's keys and values don't match; skipped", clip.name));
                continue;
            }
            out.times.resize(keys);
            cgltf_accessor_unpack_floats(ch.sampler->input, out.times.data(), keys);
            for (cgltf_size k = 1; k < keys; ++k) out.times[k] = std::max(out.times[k], out.times[k - 1]);  // never backwards
            out.values.resize(keys * per_key * n);
            cgltf_accessor_unpack_floats(ch.sampler->output, out.values.data(), out.values.size());  // normalised ints too
            clip.duration = std::max(clip.duration, out.times.back());
            clip.channels.push_back(std::move(out));
        }
        if (!clip.channels.empty()) model.animations.push_back(std::move(clip));
    }
    if (morph_channels > 0)
        model.warnings.push_back(std::format("{} morph-target (face shape) animation channels skipped: not supported yet", morph_channels));

    for (const auto& w : model.warnings) NOOR_LOG_WARN("Import", "{}: {}", path.filename().generic_string(), w);
    return model;
}

} // namespace noor::asset
