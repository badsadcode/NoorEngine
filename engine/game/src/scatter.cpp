#include "noor/game/scatter.h"

#include "noor/game/game.h"

#include <cmath>
#include <format>
#include <map>
#include <mutex>

namespace noor::game {
namespace stdfs = std::filesystem;
using asset::Json;

Result<std::shared_ptr<const asset::InstanceSet>> cached_instance_set(const stdfs::path& file) {
    struct Entry {
        std::uintmax_t size = 0;
        stdfs::file_time_type time{};
        std::shared_ptr<const asset::InstanceSet> set;
    };
    static std::mutex mutex;
    static std::map<std::string, Entry> cache;
    std::error_code ec;
    const auto size = stdfs::file_size(file, ec);
    if (ec) return fail(ErrorCode::NotFound, std::format("{} isn't there (cook the graph: Save, or noor_cli build)", file.generic_string()));
    const auto time = stdfs::last_write_time(file, ec);
    const std::string key = file.generic_string();
    {
        std::scoped_lock lock(mutex);
        if (const auto it = cache.find(key); it != cache.end() && it->second.size == size && it->second.time == time) return it->second.set;
    }
    auto loaded = asset::InstanceSet::load(file);
    if (!loaded) return std::unexpected(loaded.error());
    auto set = std::make_shared<const asset::InstanceSet>(std::move(*loaded));
    std::scoped_lock lock(mutex);
    cache[key] = {size, time, set};
    return set;
}

stdfs::path scatter_file(const Json& component, const stdfs::path& project_root) {
    const auto graph = asset::guid_field(component, "graph");
    const std::string output = component.value("output", std::string("instances"));
    if (!graph || project_root.empty()) return {};
    return cooked_graph_folder(project_root, *graph) / (output + ".instances.json");
}

std::vector<ScatterItem> scatter_items(const Json& component, const stdfs::path& project_root, std::string* warning) {
    std::vector<ScatterItem> out;
    std::set<std::uint64_t> removed;
    if (const auto r = component.find("removed"); r != component.end() && r->is_array())
        for (const auto& id : *r)
            if (id.is_string())
                if (const auto v = asset::parse_instance_id(id.get<std::string>())) removed.insert(*v);
    if (const auto file = scatter_file(component, project_root); !file.empty()) {
        if (auto set = cached_instance_set(file)) {
            const auto& s = **set;
            out.reserve(s.items.size());
            for (const auto& it : s.items) {
                if (removed.contains(it.id) || it.asset >= s.assets.size()) continue;
                out.push_back({s.assets[it.asset], it.id, false, it.position, it.rotation, it.scale});
            }
        } else if (warning) {
            *warning = set.error().message;
        }
    } else if (warning && !asset::guid_field(component, "graph")) {
        *warning = "no graph chosen";
    }
    if (const auto a = component.find("added"); a != component.end() && a->is_array()) {
        for (const auto& j : *a) {
            if (!j.is_object()) continue;
            const auto asset = asset::guid_field(j, "asset");
            const auto id = j.contains("id") && j["id"].is_string() ? asset::parse_instance_id(j["id"].get<std::string>()) : std::nullopt;
            const auto& p = j.value("position", Json::array());
            if (!asset || !id || !p.is_array() || p.size() != 3 || !p[0].is_number() || !p[1].is_number() || !p[2].is_number()) continue;
            if (removed.contains(*id)) continue;
            const float yaw = math::radians(j.value("yaw", 0.0f));
            out.push_back({*asset, *id, true, {p[0].get<float>(), p[1].get<float>(), p[2].get<float>()},
                           math::Quat::from_axis_angle({0, 1, 0}, yaw), std::clamp(j.value("scale", 1.0f), 0.01f, 100.0f)});
        }
    }
    return out;
}

std::vector<Guid> graphs_used(const scene::Scene& scene) {
    std::vector<Guid> out;
    const auto add = [&](const std::optional<Guid>& g) {
        if (g && std::ranges::find(out, *g) == out.end()) out.push_back(*g);
    };
    for (const auto& e : scene.entities) {
        if (e.has("terrain") && e.components["terrain"].is_object() && e.components["terrain"].value("base", std::string()) == "graph")
            add(asset::guid_field(e.components["terrain"], "graph"));
        if (e.has("scatter") && e.components["scatter"].is_object()) add(asset::guid_field(e.components["scatter"], "graph"));
    }
    return out;
}

std::uint64_t new_instance_id() {
    const Guid g = Guid::generate();
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | g.bytes[static_cast<std::size_t>(i)];
    return v ^ 0xadded0000000000ull;  // a different pattern from generated ids (cosmetic: ids are unique either way)
}

} // namespace noor::game
