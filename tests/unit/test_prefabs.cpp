// Prefabs (ADR-024): expansion, overrides, propagation, nesting, cycles, ids, undo.

#include "noor/asset/gltf_import.h"
#include "noor/asset/project.h"
#include "noor/asset/registry.h"
#include "noor/foundation/file_io.h"
#include "noor/scene/history.h"
#include "noor/scene/prefab.h"

#include <filesystem>
#include <map>
#include <ostream>
#include <doctest/doctest.h>

namespace stdfs = std::filesystem;
using namespace noor;

namespace {

const Guid kBuildingAsset = *Guid::parse("4529993a-ec0c-5a66-bc58-462a7823fbc6");

scene::Scene building_scene() {
    auto model = asset::import_gltf(stdfs::path(NOOR_TEST_DATA_DIR) / "badbuilding" / "osm_w117821721.glb");
    REQUIRE(model);
    scene::Scene s;
    s.name = "Street";
    scene::instantiate_model(s, kBuildingAsset, *model, "Block",
                             {{5, 0, -3}, math::Quat::from_axis_angle({0, 1, 0}, 0.4f), {1, 1, 1}});
    return s;
}

// World matrices of every mesh entity, keyed by the entity's name (names are unique in the sample).
std::map<std::string, math::Mat4> mesh_worlds(const scene::Scene& s) {
    std::map<std::string, math::Mat4> out;
    for (std::size_t i = 0; i < s.entities.size(); ++i) {
        if (s.entities[i].has("mesh")) out[s.entities[i].name] = s.world_matrix(i);
    }
    return out;
}

bool same(const math::Mat4& a, const math::Mat4& b) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (std::fabs(a.at(r, c) - b.at(r, c)) > 1e-4f) return false;
    return true;
}

struct Library {
    std::map<Guid, scene::Prefab> prefabs;
    scene::PrefabResolver resolver() {
        return [this](const Guid& id) -> const scene::Prefab* {
            const auto it = prefabs.find(id);
            return it == prefabs.end() ? nullptr : &it->second;
        };
    }
};

const scene::Entity* by_name(const scene::Scene& s, std::string_view name) {
    for (const auto& e : s.entities) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

} // namespace

TEST_CASE("guid: derive is deterministic, input-sensitive and version 8") {
    const Guid a = Guid::generate(), b = Guid::generate();
    CHECK(Guid::derive(a, b) == Guid::derive(a, b));
    CHECK(Guid::derive(a, b) != Guid::derive(b, a));
    CHECK(Guid::derive(a, b) != Guid::derive(a, Guid::generate()));
    CHECK((Guid::derive(a, b).bytes[6] >> 4) == 8);
    CHECK((Guid::derive(a, b).bytes[8] & 0xC0) == 0x80);
}

TEST_CASE("prefab: making a prefab and expanding the instance puts every mesh exactly where it was") {
    scene::Scene s = building_scene();
    const auto before = mesh_worlds(s);
    const Guid root = s.entities[0].id;
    const std::size_t total = s.entities.size();

    auto prefab = scene::make_prefab(s, root, "Block", /*replace_with_instance=*/true);
    REQUIRE(prefab);
    CHECK(prefab->entities.size() == total);
    REQUIRE(s.entities.size() == 1);
    CHECK(s.entities[0].id == root);  // references to the old root stay valid
    CHECK(s.entities[0].has("prefab"));

    Library lib;
    lib.prefabs[prefab->id] = *prefab;
    const auto expanded = scene::expand_prefabs(s, lib.resolver());
    CHECK(expanded.issues.empty());
    CHECK(expanded.instances == 1);
    CHECK(expanded.scene.entities.size() == total + 1);  // placement + the prefab's entities
    CHECK(scene::validate_structure(expanded.scene).empty());

    const auto after = mesh_worlds(expanded.scene);
    REQUIRE(after.size() == before.size());
    for (const auto& [name, world] : before) {
        CAPTURE(name);
        REQUIRE(after.contains(name));
        CHECK(same(world, after.at(name)));
    }
}

TEST_CASE("prefab: expanded ids are stable across loads and unique across instances") {
    scene::Scene s = building_scene();
    auto prefab = scene::make_prefab(s, s.entities[0].id, "Block", true);
    REQUIRE(prefab);
    s.entities.push_back(scene::make_instance(prefab->id, "Block 2", {{50, 0, 0}, {}, {1, 1, 1}}));
    Library lib;
    lib.prefabs[prefab->id] = *prefab;

    const auto first = scene::expand_prefabs(s, lib.resolver());
    const auto second = scene::expand_prefabs(*scene::Scene::from_text(s.to_text(), "reload"), lib.resolver());
    REQUIRE(first.scene.entities.size() == second.scene.entities.size());
    for (std::size_t i = 0; i < first.scene.entities.size(); ++i) {
        CHECK(first.scene.entities[i].id == second.scene.entities[i].id);
    }
    CHECK(first.instances == 2);
    CHECK(scene::validate_structure(first.scene).empty());  // no duplicate ids between the two instances
}

TEST_CASE("prefab: overrides change only their instance, and survive prefab edits") {
    scene::Scene s = building_scene();
    auto prefab = scene::make_prefab(s, s.entities[0].id, "Block", true);
    REQUIRE(prefab);
    s.entities.push_back(scene::make_instance(prefab->id, "Block 2", {{50, 0, 0}, {}, {1, 1, 1}}));

    // Instance 2: rename one wall, move it, strip its mesh; remove the balconies subtree entirely.
    const scene::Entity* wall = nullptr;
    const scene::Entity* balconies = nullptr;
    for (const auto& e : prefab->entities) {
        if (!wall && e.name.find("/wall.") != std::string::npos) wall = &e;
        if (!balconies && e.name.find("/balconies.") != std::string::npos) balconies = &e;
    }
    REQUIRE(wall);
    REQUIRE(balconies);
    auto& comp = s.entities[1].components["prefab"];
    comp["overrides"][wall->id.to_string()] = {{"name", "Painted wall"},
                                                 {"transform", {{"position", {1, 2, 3}}}},
                                                 {"components", {{"mesh", nullptr}}}};
    comp["removed"].push_back(balconies->id.to_string());

    Library lib;
    lib.prefabs[prefab->id] = *prefab;
    auto expanded = scene::expand_prefabs(s, lib.resolver());
    CHECK(expanded.issues.empty());
    const auto* painted = by_name(expanded.scene, "Painted wall");
    REQUIRE(painted);
    CHECK(painted->transform.position == math::Vec3{1, 2, 3});
    CHECK_FALSE(painted->has("mesh"));
    std::size_t walls_named_original = 0, balcony_count = 0;
    for (const auto& e : expanded.scene.entities) {
        walls_named_original += e.name == wall->name;
        balcony_count += e.name == balconies->name;
    }
    CHECK(walls_named_original == 1);  // instance 1 keeps the original
    CHECK(balcony_count == 1);         // only instance 1 still has them

    // Edit the prefab: every instance follows, except what instance 2 overrides.
    for (auto& e : lib.prefabs[prefab->id].entities) {
        if (e.id == wall->id) {
            e.name = "Renamed in prefab";
            e.tags = {"edited"};
        }
    }
    expanded = scene::expand_prefabs(s, lib.resolver());
    const auto* in_one = by_name(expanded.scene, "Renamed in prefab");
    const auto* in_two = by_name(expanded.scene, "Painted wall");
    REQUIRE(in_one);
    REQUIRE(in_two);
    CHECK(in_one->tags == std::vector<std::string>{"edited"});
    CHECK(in_two->tags == std::vector<std::string>{"edited"});  // not overridden: follows the prefab
}

TEST_CASE("prefab: an override for an entity that left the prefab is kept and reported as a warning") {
    scene::Prefab p;
    p.name = "Lamp";
    scene::Entity root;
    root.id = Guid::generate();
    root.name = "Lamp";
    p.entities.push_back(root);
    scene::Scene s;
    auto inst = scene::make_instance(p.id, "Lamp 1");
    inst.components["prefab"]["overrides"][Guid::generate().to_string()] = {{"name", "ghost"}};
    s.entities.push_back(inst);
    Library lib;
    lib.prefabs[p.id] = p;
    const auto expanded = scene::expand_prefabs(s, lib.resolver());
    REQUIRE(expanded.issues.size() == 1);
    CHECK_FALSE(expanded.issues[0].error);
    CHECK(s.entities[0].components["prefab"]["overrides"].size() == 1);  // nothing was deleted
}

TEST_CASE("prefab: nested prefabs expand, variants work, and cycles are refused") {
    // Lamp: one entity. Street: a root with two lamp instances. Variant: a street with one lamp renamed.
    scene::Prefab lamp;
    lamp.name = "Lamp";
    scene::Entity bulb;
    bulb.id = Guid::generate();
    bulb.name = "Bulb";
    bulb.components["light"] = {{"intensity", 800}};
    lamp.entities.push_back(bulb);

    scene::Prefab street;
    street.name = "Street";
    scene::Entity street_root;
    street_root.id = Guid::generate();
    street_root.name = "Street";
    street.entities.push_back(street_root);
    auto lamp_a = scene::make_instance(lamp.id, "Lamp A", {{0, 0, 0}, {}, {1, 1, 1}}, street_root.id);
    auto lamp_b = scene::make_instance(lamp.id, "Lamp B", {{10, 0, 0}, {}, {1, 1, 1}}, street_root.id);
    street.entities.push_back(lamp_a);
    street.entities.push_back(lamp_b);

    Library lib;
    lib.prefabs[lamp.id] = lamp;
    lib.prefabs[street.id] = street;

    scene::Scene s;
    s.entities.push_back(scene::make_instance(street.id, "Main street"));
    auto expanded = scene::expand_prefabs(s, lib.resolver());
    CHECK(expanded.issues.empty());
    CHECK(expanded.instances == 3);  // the street + two lamps inside it
    int bulbs = 0;
    for (const auto& e : expanded.scene.entities) bulbs += e.name == "Bulb";
    CHECK(bulbs == 2);
    CHECK(scene::validate_structure(expanded.scene).empty());

    // Cycle: the lamp now contains a street, which contains lamps...
    lib.prefabs[lamp.id].entities[0].components["prefab"] = {{"asset", street.id.to_string()}};
    expanded = scene::expand_prefabs(s, lib.resolver());
    bool cycle_reported = false;
    for (const auto& i : expanded.issues) cycle_reported |= i.error && i.message.find("cycle") != std::string::npos;
    CHECK(cycle_reported);
}

TEST_CASE("prefab: a missing prefab is an error, not a crash") {
    scene::Scene s;
    s.entities.push_back(scene::make_instance(Guid::generate(), "Ghost"));
    Library lib;
    const auto expanded = scene::expand_prefabs(s, lib.resolver());
    REQUIRE(expanded.issues.size() == 1);
    CHECK(expanded.issues[0].error);
    CHECK(expanded.scene.entities.size() == 1);
}

TEST_CASE("prefab: files round-trip byte for byte and malformed ones are rejected") {
    scene::Scene s = building_scene();
    auto prefab = scene::make_prefab(s, s.entities[0].id, "Block", false);
    REQUIRE(prefab);
    const std::string text = prefab->to_text();
    auto back = scene::Prefab::from_text(text, "memory");
    REQUIRE(back);
    CHECK(back->to_text() == text);

    auto json = *asset::parse_json(text, "memory");
    std::swap(json["entities"][0], json["entities"][1]);  // a child before its root
    CHECK_FALSE(scene::Prefab::from_text(asset::to_text(json), "memory"));
}

TEST_CASE("prefab: overrides are ordinary component edits, so undo covers them") {
    scene::Scene s = building_scene();
    auto prefab = scene::make_prefab(s, s.entities[0].id, "Block", true);
    REQUIRE(prefab);
    const std::string before = s.to_text();
    scene::History h;
    auto comp = s.entities[0].components["prefab"];
    comp["overrides"][prefab->entities[1].id.to_string()] = {{"name", "Overridden"}};
    REQUIRE(h.execute(s, scene::set_component(s.entities[0].id, "prefab", comp)));
    CHECK(s.to_text() != before);
    REQUIRE(h.undo(s));
    CHECK(s.to_text() == before);
}

TEST_CASE("registry: a .noorprefab file is an asset whose GUID is the prefab's own id") {
    const stdfs::path dir = stdfs::temp_directory_path() / ("noor_prefab_" + Guid::generate().to_string());
    auto project = asset::Project::create(dir, "PrefabGame");
    REQUIRE(project);
    scene::Prefab p;
    p.name = "Lamp";
    scene::Entity root;
    root.id = Guid::generate();
    root.name = "Lamp";
    p.entities.push_back(root);
    REQUIRE(p.save(project->assets_dir() / "Props" / "Lamp.noorprefab"));
    asset::AssetRegistry reg;
    REQUIRE(reg.scan(*project));
    const auto* rec = reg.find(p.id);
    REQUIRE(rec);
    CHECK(rec->importer == "prefab");
    CHECK(rec->path == "Assets/Props/Lamp.noorprefab");

    scene::PrefabLibrary library(*project, reg);
    REQUIRE(library.get(p.id));
    CHECK(library.get(p.id)->name == "Lamp");
    CHECK(library.get(Guid::generate()) == nullptr);
    std::error_code ec;
    stdfs::remove_all(dir, ec);
}
