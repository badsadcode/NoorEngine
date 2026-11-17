#include "noor/game/cooked_csg.h"

#include "noor/foundation/file_io.h"

#include <cstring>
#include <format>

namespace noor::game {
namespace {

// "NCSG", format version, key, vertex count, vertices, index count, indices, submesh count, then per
// submesh: first, count, has material (0/1), material id (16 bytes, as text when present).
constexpr std::uint32_t kMagic = 0x4753434Eu;  // "NCSG"
constexpr std::uint32_t kVersion = 1;

} // namespace

std::vector<std::size_t> csg_brush_children(const scene::Scene& scene, std::size_t model_index) {
    std::vector<std::size_t> out;
    if (model_index >= scene.entities.size()) return out;
    const Guid model = scene.entities[model_index].id;
    for (std::size_t j = model_index + 1; j < scene.entities.size(); ++j)
        if (scene.entities[j].parent == model && scene.entities[j].has("brush")) out.push_back(j);
    return out;
}

std::uint64_t csg_model_key(const scene::Scene& scene, std::size_t model_index) {
    std::string recipe = std::format("ncsg{} ", kVersion);
    for (const std::size_t j : csg_brush_children(scene, model_index)) {
        const auto& e = scene.entities[j];
        const auto& t = e.transform;
        recipe += e.id.to_string() + e.components.at("brush").dump();
        recipe += std::format("{} {} {} {} {} {} {} {} {} {};", t.position.x, t.position.y, t.position.z, t.rotation.x, t.rotation.y,
                              t.rotation.z, t.rotation.w, t.scale.x, t.scale.y, t.scale.z);
    }
    std::uint64_t h = 1469598103934665603ull;  // FNV-1a, 64-bit
    for (const unsigned char c : recipe) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::filesystem::path cooked_csg_path(const std::filesystem::path& project_root, const Guid& model) {
    return project_root / ".noor" / "cooked" / "csg" / (model.to_string() + ".ncsg");
}

Status write_cooked_csg(const std::filesystem::path& file, const CookedCsg& c) {
    std::string out;
    const auto put32 = [&](std::uint32_t v) { out.append(reinterpret_cast<const char*>(&v), 4); };
    put32(kMagic);
    put32(kVersion);
    out.append(reinterpret_cast<const char*>(&c.key), 8);
    put32(static_cast<std::uint32_t>(c.vertices.size()));
    out.append(reinterpret_cast<const char*>(c.vertices.data()), c.vertices.size() * sizeof(render::Vertex));
    put32(static_cast<std::uint32_t>(c.indices.size()));
    out.append(reinterpret_cast<const char*>(c.indices.data()), c.indices.size() * 4);
    put32(static_cast<std::uint32_t>(c.submeshes.size()));
    for (const auto& s : c.submeshes) {
        put32(s.first_index);
        put32(s.index_count);
        const std::string id = s.material ? s.material->to_string() : std::string();
        put32(static_cast<std::uint32_t>(id.size()));
        out += id;
    }
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    return fs::write_file_atomic(file, out, false);
}

Result<CookedCsg> read_cooked_csg(const std::filesystem::path& file) {
    auto data = fs::read_file(file, 1ull << 30);
    if (!data) return std::unexpected(data.error());
    const std::string& in = *data;
    std::size_t pos = 0;
    const auto bad = [&] { return fail(ErrorCode::ParseError, std::format("{} is not a cooked CSG model (or a damaged one)", file.filename().string())); };
    const auto get = [&](void* dst, std::size_t n) {
        if (pos + n > in.size()) return false;
        std::memcpy(dst, in.data() + pos, n);
        pos += n;
        return true;
    };
    std::uint32_t magic = 0, version = 0, count = 0;
    CookedCsg c;
    if (!get(&magic, 4) || magic != kMagic || !get(&version, 4) || version != kVersion || !get(&c.key, 8) || !get(&count, 4)) return bad();
    if (count > (1u << 26)) return bad();
    c.vertices.resize(count);
    if (!get(c.vertices.data(), count * sizeof(render::Vertex)) || !get(&count, 4) || count > (1u << 28) || count % 3 != 0) return bad();
    c.indices.resize(count);
    if (!get(c.indices.data(), count * 4ull)) return bad();
    for (const auto i : c.indices)
        if (i >= c.vertices.size()) return bad();
    if (!get(&count, 4) || count > 65536) return bad();
    for (std::uint32_t i = 0; i < count; ++i) {
        CookedCsg::Submesh s;
        std::uint32_t len = 0;
        if (!get(&s.first_index, 4) || !get(&s.index_count, 4) || !get(&len, 4) || len > 64 || pos + len > in.size()) return bad();
        if (s.first_index > c.indices.size() || s.index_count > c.indices.size() - s.first_index) return bad();
        if (len > 0) {
            auto id = Guid::parse(std::string_view(in.data() + pos, len));
            if (!id) return bad();
            s.material = *id;
            pos += len;
        }
        c.submeshes.push_back(s);
    }
    return c;
}

} // namespace noor::game
