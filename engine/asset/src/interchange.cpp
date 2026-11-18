#include "noor/asset/interchange.h"

#include "noor/asset/gltf_import.h"
#include "noor/foundation/file_io.h"
#include "noor/foundation/sha256.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <set>

namespace noor::asset {
namespace {
namespace stdfs = std::filesystem;

std::string str(const Json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

std::optional<math::Vec3> vec3(const Json& j) {
    if (!j.is_array() || j.size() != 3) return std::nullopt;
    for (const auto& v : j)
        if (!v.is_number()) return std::nullopt;
    return math::Vec3{j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

// A path from a manifest may only name a file in the manifest's own folder or below it.
bool safe_relative(const std::string& path) {
    if (path.empty()) return false;
    const stdfs::path p(path);
    if (p.is_absolute() || p.has_root_name() || p.has_root_directory()) return false;
    for (const auto& part : p)
        if (part == "..") return false;
    return true;
}

const std::set<std::string>& known_kinds() {
    static const std::set<std::string> kinds{"building", "ship", "vehicle", "level", "prop", "rock",
                                             "vegetation.tree", "vegetation.plant", "vegetation.scatter",
                                             "road", "bridge", "material", "terrain", "character"};
    return kinds;
}

// Top-level keys the spec defines; any other object-valued key is a tool's own block.
const std::set<std::string>& spec_keys() {
    static const std::set<std::string> keys{"noor_interchange", "id", "name", "kind", "generator", "recipe", "created", "space",
                                            "bounds", "files", "lods", "collision", "markers", "tags", "license",
                                            "validation", "provenance", "family", "wind"};
    return keys;
}

} // namespace

bool ManifestRead::has_errors() const {
    return std::ranges::any_of(issues, [](const ManifestIssue& i) { return i.error; });
}

stdfs::path manifest_path_for_model(const stdfs::path& model) {
    return model.parent_path() / (model.stem().string() + ".noor.json");
}

Result<ManifestRead> read_manifest(const stdfs::path& path) {
    auto text = fs::read_file(path, 4 << 20);
    if (!text) return std::unexpected(text.error());
    auto parsed = parse_json(*text, path.generic_string());
    if (!parsed) return std::unexpected(parsed.error());
    const Json& j = *parsed;
    if (!j.is_object() || !j.contains("noor_interchange")) {
        return fail(ErrorCode::ParseError, std::format("'{}' isn't a Noor Interchange manifest (no \"noor_interchange\")", path.generic_string()));
    }

    ManifestRead out;
    auto& m = out.manifest;
    auto error = [&](std::string field, std::string message) { out.issues.push_back({std::move(field), std::move(message), true}); };
    auto warn = [&](std::string field, std::string message) { out.issues.push_back({std::move(field), std::move(message), false}); };
    m.raw = j;
    const stdfs::path folder = path.parent_path();

    // Version: 0.x loads; a newer minor's extra fields are optional by the spec's rules.
    m.version = j["noor_interchange"].is_string() ? j["noor_interchange"].get<std::string>() : std::string{};
    int major = -1, minor = -1;
    const char* const vb = m.version.data();
    const char* const ve = vb + m.version.size();
    const auto [after_major, e1] = std::from_chars(vb, ve, major);
    const bool parsed_version = e1 == std::errc{} && after_major < ve && *after_major == '.' &&
                                std::from_chars(after_major + 1, ve, minor).ec == std::errc{};
    if (!parsed_version) {
        error("noor_interchange", std::format("\"{}\" isn't a version like \"0.4\"", j["noor_interchange"].dump()));
    } else if (major != kInterchangeMajor) {
        error("noor_interchange", std::format("version {} needs a newer NoorEngine (this one reads {}.x)", m.version, kInterchangeMajor));
    } else if (minor > kInterchangeMinor) {
        warn("noor_interchange", std::format("version {} is newer than this engine knows ({}.{}): fields it doesn't know are ignored",
                                             m.version, kInterchangeMajor, kInterchangeMinor));
    }

    if (auto id = guid_field(j, "id")) {
        m.id = *id;
    } else {
        error("id", "missing or not a UUID: the asset's id must stay the same when it's regenerated");
    }
    m.name = str(j, "name");
    if (m.name.empty()) error("name", "missing");
    m.kind = str(j, "kind");
    if (m.kind.empty()) {
        error("kind", "missing");
    } else if (!known_kinds().contains(m.kind)) {
        warn("kind", std::format("\"{}\" isn't in the spec's list of kinds (add it there first)", m.kind));
    }

    if (const auto g = j.find("generator"); g != j.end() && g->is_object()) {
        m.generator.tool = str(*g, "tool");
        m.generator.version = str(*g, "version");
        m.generator.commit = str(*g, "commit");
        if (const auto d = str(*g, "determinism"); !d.empty()) m.generator.determinism = d;
        if (m.generator.tool.empty()) warn("generator.tool", "missing");
        if (m.generator.determinism != "cross_platform" && m.generator.determinism != "same_platform" && m.generator.determinism != "none")
            warn("generator.determinism", std::format("\"{}\" isn't cross_platform, same_platform or none", m.generator.determinism));
    } else {
        warn("generator", "missing: nobody will know which tool made this");
    }

    if (const auto r = j.find("recipe"); r != j.end() && r->is_object()) {
        m.recipe.file = str(*r, "file");
        m.recipe.sha256 = str(*r, "sha256");
        if (const auto s = r->find("seed"); s != r->end()) {
            if (s->is_number_unsigned()) {
                m.recipe.seed = s->get<std::uint64_t>();
            } else if (s->is_number_integer()) {
                error("recipe.seed", "negative: seeds are unsigned 64-bit integers");
            } else {
                error("recipe.seed", "must be a whole number written as an integer (a float or a string would lose or change bits)");
            }
        }
    }

    m.created = str(j, "created");

    if (const auto s = j.find("space"); s != j.end() && s->is_object()) {
        const auto up = str(*s, "up"), hand = str(*s, "handedness"), units = str(*s, "units");
        if (!up.empty() && up != "+Y") error("space.up", std::format("\"{}\": Noor assets are +Y up", up));
        if (!hand.empty() && hand != "right") error("space.handedness", std::format("\"{}\": Noor assets are right-handed", hand));
        if (!units.empty() && units != "meters" && units != "metres") error("space.units", std::format("\"{}\": Noor assets are in metres", units));
    }

    if (const auto b = j.find("bounds"); b != j.end() && b->is_object()) {
        m.bounds_min = vec3(b->value("min", Json()));
        m.bounds_max = vec3(b->value("max", Json()));
        if (!m.bounds_min || !m.bounds_max) {
            warn("bounds", "min and max must be three numbers each");
        } else if (m.bounds_min->x > m.bounds_max->x || m.bounds_min->y > m.bounds_max->y || m.bounds_min->z > m.bounds_max->z) {
            error("bounds", "min is larger than max");
        }
    }

    // Files: inside the folder, present, unchanged.
    if (const auto f = j.find("files"); f != j.end() && f->is_array()) {
        for (std::size_t i = 0; i < f->size(); ++i) {
            const Json& e = (*f)[i];
            const std::string field = std::format("files[{}]", i);
            InterchangeFile file{str(e, "path"), str(e, "role"), e.value("lod", -1), str(e, "sha256")};
            if (!safe_relative(file.path)) {
                error(field, std::format("path \"{}\" must be a file in the asset's folder", file.path));
                continue;
            }
            std::error_code ec;
            const stdfs::path full = folder / stdfs::path(file.path);
            if (!stdfs::is_regular_file(full, ec)) {
                if (file.role == "model") {
                    error(field, std::format("{} (the model) is missing", file.path));
                } else {
                    warn(field, std::format("{} is listed but not there", file.path));
                }
            } else if (!file.sha256.empty()) {
                if (auto data = fs::read_file(full, 1ull << 30); data && Sha256::hex(*data) != file.sha256) {
                    warn(field, std::format("{} has changed since it was exported (its SHA-256 doesn't match)", file.path));
                }
            }
            if (file.role == "lod" && file.lod < 1) error(field, "a level file needs \"lod\": 1 or more");
            m.files.push_back(std::move(file));
        }
    } else {
        error("files", "missing: a manifest lists its files");
    }
    const auto model = std::ranges::find(m.files, std::string("model"), &InterchangeFile::role);
    if (model == m.files.end() && !m.files.empty()) error("files", "no file has the role \"model\"");

    // The recipe file, if named, must be listed safely and match its hash.
    if (!m.recipe.file.empty()) {
        if (!safe_relative(m.recipe.file)) {
            error("recipe.file", std::format("\"{}\" must be a file in the asset's folder", m.recipe.file));
        } else if (auto data = fs::read_file(folder / stdfs::path(m.recipe.file), 64 << 20); !data) {
            warn("recipe.file", std::format("{} isn't there", m.recipe.file));
        } else if (!m.recipe.sha256.empty() && Sha256::hex(*data) != m.recipe.sha256) {
            warn("recipe.sha256", std::format("{} has changed since the asset was made from it", m.recipe.file));
        }
    }

    // Levels: from "lods" (distances, triangles), matched to their files.
    if (const auto l = j.find("lods"); l != j.end() && l->is_array()) {
        std::set<int> seen;
        for (std::size_t i = 0; i < l->size(); ++i) {
            const Json& e = (*l)[i];
            const std::string field = std::format("lods[{}]", i);
            InterchangeLevel level;
            level.level = e.value("level", -1);
            if (level.level < 0 || !seen.insert(level.level).second) {
                error(field, "\"level\" must be 0 or more, and each level listed once");
                continue;
            }
            if (const auto t = e.find("triangles"); t != e.end() && t->is_number_unsigned()) level.triangles = t->get<std::uint64_t>();
            if (const auto d = e.find("max_distance"); d != e.end()) {
                if (!d->is_number() || d->get<double>() < 0 || !std::isfinite(d->get<double>())) {
                    error(field + ".max_distance", "must be a distance in metres (0 or more)");
                } else if (d->get<double>() > 0) {
                    level.max_distance = d->get<double>();
                }
            }
            level.billboard = e.value("billboard", false);
            if (level.level == 0) {
                if (model != m.files.end()) level.file = model->path;
            } else if (const auto file = std::ranges::find_if(m.files, [&](const InterchangeFile& f) { return f.role == "lod" && f.lod == level.level; });
                       file != m.files.end()) {
                level.file = file->path;
            } else {
                warn(field, std::format("level {} has no file (role \"lod\", \"lod\": {})", level.level, level.level));
            }
            m.lods.push_back(std::move(level));
        }
        std::ranges::sort(m.lods, {}, &InterchangeLevel::level);
        // Distances are optional (v0.4); without them an engine picks its own. Some but not all is odd.
        const bool any_distance = std::ranges::any_of(m.lods, [](const InterchangeLevel& l) { return l.max_distance.has_value(); });
        for (std::size_t i = 0; i < m.lods.size(); ++i) {
            if (m.lods[i].level != static_cast<int>(i)) {
                error("lods", std::format("levels must be 0, 1, 2... without gaps (level {} is missing)", i));
                break;
            }
            if (i + 1 < m.lods.size()) {
                if (!m.lods[i].max_distance) {
                    if (any_distance) warn("lods", std::format("level {} has no max_distance but other levels do", i));
                } else if (m.lods[i + 1].max_distance && *m.lods[i + 1].max_distance <= *m.lods[i].max_distance) {
                    error("lods", std::format("level {}'s max_distance must be larger than level {}'s", i + 1, i));
                }
            }
        }
    } else if (model != m.files.end()) {
        m.lods.push_back({0, model->path, 0, std::nullopt, false});  // no list: just the model
    }
    // Level files listed without a "lods" entry.
    for (const auto& f : m.files) {
        if (f.role == "lod" && std::ranges::none_of(m.lods, [&](const InterchangeLevel& l) { return l.level == f.lod; }))
            warn("lods", std::format("{} is a level file, but \"lods\" doesn't list level {}", f.path, f.lod));
    }

    m.collision = str(j, "collision");
    if (const auto fam = j.find("family"); fam != j.end() && fam->is_object()) {
        m.family = InterchangeManifest::Family{str(*fam, "name"), str(*fam, "variant"), fam->value("count", 0)};
    }
    if (const auto w = j.find("wind"); w != j.end()) m.wind = *w;
    if (const auto t = j.find("tags"); t != j.end() && t->is_array()) {
        for (const auto& tag : *t)
            if (tag.is_string()) m.tags.push_back(tag.get<std::string>());
    }
    if (const auto lic = j.find("license"); lic != j.end() && lic->is_object()) {
        m.license = {str(*lic, "spdx"), str(*lic, "author"), str(*lic, "attribution")};
    }
    if (const auto v = j.find("validation"); v != j.end()) m.validation = *v;
    if (const auto p = j.find("provenance"); p != j.end()) m.provenance = *p;
    for (const auto& [key, value] : j.items()) {
        if (!spec_keys().contains(key) && value.is_object()) m.tool_blocks.push_back(key);
    }
    return out;
}

std::vector<LevelCheck> check_levels(const ManifestRead& read, const stdfs::path& folder) {
    std::vector<LevelCheck> checks;
    for (const auto& level : read.manifest.lods) {
        LevelCheck c;
        c.level = level.level;
        c.file = level.file;
        c.manifest_triangles = level.triangles;
        c.max_distance = level.max_distance;
        c.billboard = level.billboard;
        if (level.file.empty() || !safe_relative(level.file)) {
            c.issues.push_back("no file for this level");
            checks.push_back(std::move(c));
            continue;
        }
        const stdfs::path full = folder / stdfs::path(level.file);
        std::error_code ec;
        c.exists = stdfs::is_regular_file(full, ec);
        if (!c.exists) {
            c.issues.push_back(std::format("{} isn't there", level.file));
            checks.push_back(std::move(c));
            continue;
        }
        c.bytes = stdfs::file_size(full, ec);
        auto model = import_gltf(full);
        if (!model) {
            c.issues.push_back(std::format("doesn't import: {}", model.error().message));
            checks.push_back(std::move(c));
            continue;
        }
        c.triangles = model->render_triangles();
        c.materials = static_cast<int>(model->materials.size());
        c.images = static_cast<int>(model->images.size());
        const ImportedNode* root = nullptr;
        for (const auto& node : model->nodes) {
            if (node.is_collision()) ++c.collision_nodes;
            if (!node.noor.is_object()) continue;
            if (!root && node.noor.contains("lod")) root = &node;
            if (const auto p = node.noor.find("part"); p != node.noor.end() && p->is_string() &&
                std::ranges::find(c.parts, p->get<std::string>()) == c.parts.end())
                c.parts.push_back(p->get<std::string>());
        }
        if (c.manifest_triangles && c.triangles != c.manifest_triangles)
            c.issues.push_back(std::format("has {} triangles; the manifest says {}", c.triangles, c.manifest_triangles));
        if (root) {
            if (root->noor.value("lod", -1) != level.level)
                c.issues.push_back(std::format("its root node says level {}", root->noor.value("lod", -1)));
            if (const auto d = root->noor.find("max_distance"); d != root->noor.end() && d->is_number()) {
                const double node_d = d->get<double>();
                const double manifest_d = level.max_distance.value_or(0.0);
                if (std::abs(node_d - manifest_d) > 1e-3)
                    c.issues.push_back(std::format("its root node says max_distance {} m; the manifest says {} m", node_d, manifest_d));
            }
            if (root->noor.value("billboard", false) != level.billboard)
                c.issues.push_back("its root node and the manifest disagree on \"billboard\"");
        }
        if (level.level > 0 && c.collision_nodes > 0)
            c.issues.push_back("has collision nodes: collision belongs in the model file (level 0)");
        for (const auto& w : model->warnings) c.issues.push_back(w);
        checks.push_back(std::move(c));
    }
    return checks;
}

Json to_json(const ManifestRead& read) {
    const auto& m = read.manifest;
    Json j{{"version", m.version}, {"id", m.id.to_string()}, {"name", m.name}, {"kind", m.kind}};
    j["generator"] = {{"tool", m.generator.tool}, {"version", m.generator.version}, {"commit", m.generator.commit},
                      {"determinism", m.generator.determinism}};
    j["recipe"] = {{"file", m.recipe.file}, {"seed", m.recipe.seed ? Json(*m.recipe.seed) : Json()}, {"sha256", m.recipe.sha256}};
    j["created"] = m.created;
    j["bounds"] = m.bounds_min && m.bounds_max
                      ? Json{{"min", {m.bounds_min->x, m.bounds_min->y, m.bounds_min->z}}, {"max", {m.bounds_max->x, m.bounds_max->y, m.bounds_max->z}}}
                      : Json();
    Json lods = Json::array();
    for (const auto& l : m.lods)
        lods.push_back({{"level", l.level}, {"file", l.file}, {"triangles", l.triangles},
                        {"max_distance", l.max_distance ? Json(*l.max_distance) : Json()}, {"billboard", l.billboard}});
    j["lods"] = std::move(lods);
    j["collision"] = m.collision;
    j["family"] = m.family ? Json{{"name", m.family->name}, {"variant", m.family->variant}, {"count", m.family->count}} : Json();
    j["wind"] = m.wind;
    j["tags"] = m.tags;
    j["license"] = {{"spdx", m.license.spdx}, {"author", m.license.author}, {"attribution", m.license.attribution}};
    j["validation"] = m.validation;
    j["provenance"] = m.provenance;
    j["tool_blocks"] = m.tool_blocks;
    Json issues = Json::array();
    for (const auto& i : read.issues) issues.push_back({{"field", i.field}, {"message", i.message}, {"error", i.error}});
    j["issues"] = std::move(issues);
    return j;
}

Json to_json(const std::vector<LevelCheck>& levels) {
    Json list = Json::array();
    for (const auto& c : levels) {
        list.push_back({{"level", c.level}, {"file", c.file}, {"exists", c.exists}, {"bytes", c.bytes}, {"triangles", c.triangles},
                        {"manifest_triangles", c.manifest_triangles}, {"max_distance", c.max_distance ? Json(*c.max_distance) : Json()},
                        {"billboard", c.billboard}, {"materials", c.materials}, {"images", c.images},
                        {"collision_nodes", c.collision_nodes}, {"parts", c.parts}, {"issues", c.issues}});
    }
    return list;
}

} // namespace noor::asset
