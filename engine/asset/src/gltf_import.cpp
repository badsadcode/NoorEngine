#include "noor/asset/gltf_import.h"

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
bool append_primitive(const cgltf_primitive& prim, ImportedMesh& out, std::vector<std::string>& warnings) {
    if (prim.type != cgltf_primitive_type_triangles) {
        warnings.push_back(std::format("mesh '{}': skipped a non-triangle primitive", out.name));
        return false;
    }
    const cgltf_accessor* pos = nullptr;
    const cgltf_accessor* nrm = nullptr;
    const cgltf_accessor* col = nullptr;
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        const auto& a = prim.attributes[i];
        if (a.type == cgltf_attribute_type_position && a.index == 0) pos = a.data;
        if (a.type == cgltf_attribute_type_normal && a.index == 0) nrm = a.data;
        if (a.type == cgltf_attribute_type_color && a.index == 0) col = a.data;
    }
    if (!pos || pos->count == 0) {
        warnings.push_back(std::format("mesh '{}': primitive without positions skipped", out.name));
        return false;
    }
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
        out.vertices.push_back(vert);
    }

    const std::size_t index_start = out.indices.size();
    if (prim.indices) {
        if (prim.indices->count % 3 != 0) {
            warnings.push_back(std::format("mesh '{}': index count not a multiple of 3; primitive skipped", out.name));
            out.vertices.resize(first);
            return false;
        }
        out.indices.reserve(index_start + prim.indices->count);
        for (cgltf_size i = 0; i < prim.indices->count; ++i) {
            const cgltf_size idx = cgltf_accessor_read_index(prim.indices, i);
            if (idx >= count) {
                warnings.push_back(std::format("mesh '{}': index {} out of range; primitive skipped", out.name, idx));
                out.vertices.resize(first);
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
    return true;
}

} // namespace

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
    model.meshes.reserve(data->meshes_count);
    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        ImportedMesh mesh;
        mesh.name = data->meshes[m].name ? data->meshes[m].name : std::format("mesh_{}", m);
        for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p) {
            append_primitive(data->meshes[m].primitives[p], mesh, model.warnings);
        }
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
        model.nodes.push_back(std::move(node));
        for (cgltf_size c = n->children_count; c-- > 0;) stack.push_back({n->children[c], index});
    }

    for (const auto& w : model.warnings) NOOR_LOG_WARN("Import", "{}: {}", path.filename().generic_string(), w);
    return model;
}

} // namespace noor::asset
