#include "noor/asset/gltf_export.h"

#include "noor/asset/project.h"
#include "noor/foundation/file_io.h"
#include "noor/foundation/sha256.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <map>
#include <unordered_map>

namespace noor::asset {
namespace stdfs = std::filesystem;

namespace {

// The binary chunk, built up view by view (each view 4-byte aligned, as glTF requires).
struct Bin {
    std::string bytes;
    Json views = Json::array();
    int view(const void* data, std::size_t size, int target = 0) {
        while (bytes.size() % 4) bytes.push_back('\0');
        Json v{{"buffer", 0}, {"byteOffset", bytes.size()}, {"byteLength", size}};
        if (target) v["target"] = target;
        bytes.append(static_cast<const char*>(data), size);
        views.push_back(std::move(v));
        return static_cast<int>(views.size()) - 1;
    }
};

std::string lower_ext(const stdfs::path& p) {
    std::string e = p.extension().string();
    std::ranges::transform(e, e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

std::string iso_now() { return utc_now_iso8601(); }

} // namespace

Result<Json> export_noor_glb(const ExportModel& model, const AssetFileResolver& resolve, const stdfs::path& glb,
                             std::vector<std::string>* warnings) {
    const auto warn = [&](std::string w) {
        if (warnings) warnings->push_back(std::move(w));
    };
    if (model.indices.empty()) return fail(ErrorCode::InvalidArgument, "nothing to export: the model has no triangles");
    Bin bin;
    Json accessors = Json::array(), meshes = Json::array(), materials = Json::array(), textures = Json::array(), images = Json::array();
    const auto accessor = [&](int view, int component, std::size_t count, const char* type, const Json& extra = nullptr) {
        Json a{{"bufferView", view}, {"componentType", component}, {"count", count}, {"type", type}};
        if (extra.is_object())
            for (auto& [k, v] : extra.items()) a[k] = v;
        accessors.push_back(std::move(a));
        return static_cast<int>(accessors.size()) - 1;
    };

    // Images (embedded once each) and textures.
    std::map<stdfs::path, int> texture_of;
    const auto texture = [&](const TextureRef& ref, const char* what, const stdfs::path& material) -> int {
        if (ref.empty()) return -1;
        if (ref.embedded_image >= 0) {
            warn(std::format("{}: the {} map comes from inside a GLB and isn't exported", material.filename().string(), what));
            return -1;
        }
        if (const auto it = texture_of.find(ref.file); it != texture_of.end()) return it->second;
        const std::string ext = lower_ext(ref.file);
        const char* mime = ext == ".png" ? "image/png" : (ext == ".jpg" || ext == ".jpeg") ? "image/jpeg" : nullptr;
        auto bytes = mime ? fs::read_file(ref.file, 512ull << 20) : Result<std::string>(std::unexpected(Error{}));
        if (!mime || !bytes) {
            warn(std::format("{}: the {} map {} isn't PNG or JPEG (or can't be read) and isn't exported", material.filename().string(), what,
                             ref.file.filename().string()));
            return texture_of[ref.file] = -1;
        }
        images.push_back({{"bufferView", bin.view(bytes->data(), bytes->size())}, {"mimeType", mime}, {"name", ref.file.stem().string()}});
        textures.push_back({{"source", static_cast<int>(images.size()) - 1}, {"sampler", 0}});
        return texture_of[ref.file] = static_cast<int>(textures.size()) - 1;
    };

    // Materials, one per distinct .noormat (index into `materials`), with their UV transforms.
    struct LoadedMaterial {
        int index = -1;
        math::Vec2 uv_scale{1, 1}, uv_offset{0, 0};
    };
    std::map<stdfs::path, LoadedMaterial> material_of;
    const auto material = [&](const stdfs::path& file) -> LoadedMaterial {
        if (const auto it = material_of.find(file); it != material_of.end()) return it->second;
        LoadedMaterial out;
        Json m{{"name", file.empty() ? std::string("Default") : file.stem().string()}};
        Material mat;
        if (!file.empty()) {
            std::vector<std::string> mw;
            if (auto loaded = load_material(file, resolve, &mw)) mat = *loaded;
            else warn(std::format("{}: {}; exported as a plain material", file.filename().string(), loaded.error().describe()));
            for (auto& w : mw) warn(std::move(w));
        }
        Json pbr{{"baseColorFactor", {mat.base_color.x, mat.base_color.y, mat.base_color.z, mat.base_color.w}},
                 {"metallicFactor", mat.metallic},
                 {"roughnessFactor", mat.roughness}};
        if (const int t = texture(mat.base_color_map, "base colour", file); t >= 0) pbr["baseColorTexture"] = {{"index", t}};
        if (const int t = texture(mat.metallic_roughness_map, "metallic-roughness", file); t >= 0) pbr["metallicRoughnessTexture"] = {{"index", t}};
        if (!mat.roughness_map.empty() || !mat.metallic_map.empty())
            warn(std::format("{}: separate roughness/metallic maps have no glTF equivalent and aren't exported (pack them into an ORM map)",
                             file.filename().string()));
        m["pbrMetallicRoughness"] = std::move(pbr);
        if (const int t = texture(mat.normal_map, "normal", file); t >= 0) m["normalTexture"] = {{"index", t}, {"scale", mat.normal_scale}};
        if (const int t = texture(mat.occlusion_map, "occlusion", file); t >= 0) m["occlusionTexture"] = {{"index", t}, {"strength", mat.occlusion_strength}};
        if (mat.emissive.x > 0 || mat.emissive.y > 0 || mat.emissive.z > 0) {
            m["emissiveFactor"] = {std::min(1.0f, mat.emissive.x), std::min(1.0f, mat.emissive.y), std::min(1.0f, mat.emissive.z)};
            if (mat.emissive_strength != 1.0f) m["extensions"]["KHR_materials_emissive_strength"] = {{"emissiveStrength", mat.emissive_strength}};
            if (const int t = texture(mat.emissive_map, "emissive", file); t >= 0) m["emissiveTexture"] = {{"index", t}};
        }
        if (mat.alpha == AlphaMode::Cutout) {
            m["alphaMode"] = "MASK";
            m["alphaCutoff"] = mat.alpha_cutoff;
        } else if (mat.alpha == AlphaMode::Blend) {
            m["alphaMode"] = "BLEND";
        }
        if (mat.double_sided) m["doubleSided"] = true;
        materials.push_back(std::move(m));
        out.index = static_cast<int>(materials.size()) - 1;
        out.uv_scale = mat.uv_scale;
        out.uv_offset = mat.uv_offset;
        return material_of[file] = out;
    };

    // The visible mesh: one primitive per part, each with its own vertices (the part's material UV
    // transform baked in).
    Json primitives = Json::array();
    math::Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    std::size_t triangles = 0;
    for (const auto& part : model.parts) {
        if (part.index_count == 0 || part.first_index + part.index_count > model.indices.size()) continue;
        const LoadedMaterial mat = material(part.material);
        std::unordered_map<std::uint32_t, std::uint32_t> remap;
        std::vector<float> pos, nrm, uv, tan;
        std::vector<std::uint32_t> idx;
        math::Vec3 plo{1e30f, 1e30f, 1e30f}, phi{-1e30f, -1e30f, -1e30f};
        for (std::uint32_t i = part.first_index; i < part.first_index + part.index_count; ++i) {
            const std::uint32_t src = model.indices[i];
            auto [it, added] = remap.try_emplace(src, static_cast<std::uint32_t>(remap.size()));
            if (added) {
                const MeshVertex& v = model.vertices[src];
                pos.insert(pos.end(), {v.position.x, v.position.y, v.position.z});
                nrm.insert(nrm.end(), {v.normal.x, v.normal.y, v.normal.z});
                uv.insert(uv.end(), {v.uv.x * mat.uv_scale.x + mat.uv_offset.x, v.uv.y * mat.uv_scale.y + mat.uv_offset.y});
                tan.insert(tan.end(), {v.tangent.x, v.tangent.y, v.tangent.z, v.tangent.w < 0 ? -1.0f : 1.0f});
                plo = {std::min(plo.x, v.position.x), std::min(plo.y, v.position.y), std::min(plo.z, v.position.z)};
                phi = {std::max(phi.x, v.position.x), std::max(phi.y, v.position.y), std::max(phi.z, v.position.z)};
            }
            idx.push_back(it->second);
        }
        lo = {std::min(lo.x, plo.x), std::min(lo.y, plo.y), std::min(lo.z, plo.z)};
        hi = {std::max(hi.x, phi.x), std::max(hi.y, phi.y), std::max(hi.z, phi.z)};
        triangles += idx.size() / 3;
        const std::size_t n = remap.size();
        const int p = accessor(bin.view(pos.data(), pos.size() * 4, 34962), 5126, n, "VEC3", Json{{"min", {plo.x, plo.y, plo.z}}, {"max", {phi.x, phi.y, phi.z}}});
        const int nr = accessor(bin.view(nrm.data(), nrm.size() * 4, 34962), 5126, n, "VEC3");
        const int t = accessor(bin.view(uv.data(), uv.size() * 4, 34962), 5126, n, "VEC2");
        const int tg = accessor(bin.view(tan.data(), tan.size() * 4, 34962), 5126, n, "VEC4");
        const int ix = accessor(bin.view(idx.data(), idx.size() * 4, 34963), 5125, idx.size(), "SCALAR");
        primitives.push_back({{"attributes", {{"POSITION", p}, {"NORMAL", nr}, {"TEXCOORD_0", t}, {"TANGENT", tg}}}, {"indices", ix}, {"material", mat.index}});
    }
    if (primitives.empty()) return fail(ErrorCode::InvalidArgument, "nothing to export: no parts with triangles");
    meshes.push_back({{"name", model.name}, {"primitives", std::move(primitives)}});
    Json nodes = Json::array({{{"name", model.name}, {"mesh", 0}}});
    if (model.collision) {
        // Positions only: all triangles in one list (Godot builds a body from "-colonly"; importers skip drawing it).
        std::vector<float> pos;
        pos.reserve(model.vertices.size() * 3);
        for (const auto& v : model.vertices) pos.insert(pos.end(), {v.position.x, v.position.y, v.position.z});
        const int p = accessor(bin.view(pos.data(), pos.size() * 4, 34962), 5126, model.vertices.size(), "VEC3",
                               Json{{"min", {lo.x, lo.y, lo.z}}, {"max", {hi.x, hi.y, hi.z}}});
        const int ix = accessor(bin.view(model.indices.data(), model.indices.size() * 4, 34963), 5125, model.indices.size(), "SCALAR");
        meshes.push_back({{"name", model.name + "-colonly"}, {"primitives", Json::array({{{"attributes", {{"POSITION", p}}}, {"indices", ix}}})}});
        nodes.push_back({{"name", model.name + "-colonly"}, {"mesh", 1}, {"extras", {{"noor", {{"v", 1}, {"collision", "mesh"}}}}}});
    }

    Json gltf{{"asset", {{"version", "2.0"}, {"generator", "NoorEngine " + model.generator_version}}},
              {"scene", 0},
              {"scenes", Json::array({{{"nodes", model.collision ? Json::array({0, 1}) : Json::array({0})}}})},
              {"nodes", std::move(nodes)},
              {"meshes", std::move(meshes)},
              {"accessors", std::move(accessors)},
              {"materials", std::move(materials)},
              {"buffers", Json::array({{{"byteLength", 0}}})}};
    if (!images.empty()) {
        gltf["images"] = std::move(images);
        gltf["textures"] = std::move(textures);
        gltf["samplers"] = Json::array({{{"magFilter", 9729}, {"minFilter", 9987}, {"wrapS", 10497}, {"wrapT", 10497}}});
    }
    bool emissive_strength = false;
    for (const auto& m : gltf["materials"])
        if (m.contains("extensions")) emissive_strength = true;
    if (emissive_strength) gltf["extensionsUsed"] = Json::array({"KHR_materials_emissive_strength"});
    while (bin.bytes.size() % 4) bin.bytes.push_back('\0');
    gltf["buffers"][0]["byteLength"] = bin.bytes.size();
    gltf["bufferViews"] = std::move(bin.views);

    // GLB: header, JSON chunk (padded with spaces), BIN chunk (padded with zeros).
    std::string json = gltf.dump();
    while (json.size() % 4) json.push_back(' ');
    std::string out;
    const auto put = [&](std::uint32_t v) { out.append(reinterpret_cast<const char*>(&v), 4); };
    put(0x46546C67u);  // "glTF"
    put(2);
    put(static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + bin.bytes.size()));
    put(static_cast<std::uint32_t>(json.size()));
    put(0x4E4F534Au);  // "JSON"
    out += json;
    put(static_cast<std::uint32_t>(bin.bytes.size()));
    put(0x004E4942u);  // "BIN\0"
    out += bin.bytes;
    std::error_code ec;
    stdfs::create_directories(glb.parent_path(), ec);
    if (auto w = fs::write_file_atomic(glb, out, false); !w) return std::unexpected(w.error());

    // The manifest (spec section 4). Hand-made in the editor: no recipe or seed.
    Json manifest{{"noor_interchange", "0.3"},
                  {"id", model.id.to_string()},
                  {"name", model.name},
                  {"kind", model.kind},
                  {"generator", {{"tool", "NoorEngine"}, {"version", model.generator_version}}},
                  {"created", iso_now()},
                  {"space", {{"up", "+Y"}, {"forward", "+Z"}, {"handedness", "right"}, {"units", "meters"}}},
                  {"bounds", {{"min", {lo.x, lo.y, lo.z}}, {"max", {hi.x, hi.y, hi.z}}}},
                  {"files", Json::array({{{"path", glb.filename().generic_string()}, {"role", "model"}, {"sha256", Sha256::hex(out)}}})},
                  {"lods", Json::array({{{"level", 0}, {"triangles", triangles}}})},
                  {"collision", model.collision ? "mesh" : "none"},
                  {"markers", Json::object()},
                  {"license", {{"spdx", "NOASSERTION"}, {"author", ""}, {"attribution", ""}}},
                  {"validation", {{"gltf_validator", "not_run"}, {"errors", 0}, {"warnings", 0}}}};
    stdfs::path manifest_path = glb;
    manifest_path.replace_extension(".noor.json");
    if (auto w = fs::write_file_atomic(manifest_path, manifest.dump(2) + "\n", false); !w) return std::unexpected(w.error());
    return manifest;
}

} // namespace noor::asset
