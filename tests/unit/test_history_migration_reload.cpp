// Milestone 2, part 2: undo/redo (spec 26.4 invariants), scene format migrations, file watching.

#include "noor/asset/file_watcher.h"
#include "noor/asset/gltf_import.h"
#include "noor/foundation/file_io.h"
#include "noor/scene/history.h"
#include "noor/scene/scene.h"

#include <filesystem>
#include <random>
#include <thread>
#include <ostream>
#include <doctest/doctest.h>

namespace stdfs = std::filesystem;
using namespace noor;
using namespace std::chrono_literals;

namespace {

scene::Scene building_scene() {
    auto model = asset::import_gltf(stdfs::path(NOOR_TEST_DATA_DIR) / "badbuilding" / "osm_w117821721.glb");
    REQUIRE(model);
    scene::Scene s;
    s.name = "Undo";
    scene::instantiate_model(s, *Guid::parse("4529993a-ec0c-5a66-bc58-462a7823fbc6"), *model, "Block");
    return s;
}

} // namespace

// ---------------------------------------------------------------- undo / redo

TEST_CASE("history: undo and redo of a single move") {
    scene::Scene s = building_scene();
    scene::History h;
    const Guid id = s.entities[3].id;
    const std::string before = s.to_text();
    scene::Transform t = s.entities[3].transform;
    t.position = {10, 20, 30};
    REQUIRE(h.execute(s, scene::set_transform(id, t)));
    CHECK(s.entities[3].transform.position == math::Vec3{10, 20, 30});
    CHECK(h.undo_label() == "Move");
    REQUIRE(h.undo(s));
    CHECK(s.to_text() == before);
    REQUIRE(h.redo(s));
    CHECK(s.entities[3].transform.position == math::Vec3{10, 20, 30});
}

TEST_CASE("history: a transaction is one undo step") {
    scene::Scene s = building_scene();
    scene::History h;
    const std::string before = s.to_text();
    h.begin_transaction("Rename all walls");
    for (const auto& e : std::vector(s.entities)) {
        if (e.name.find("wall") != std::string::npos) h.execute(s, scene::rename(e.id, "Wall"));
    }
    h.end_transaction();
    CHECK(h.undo_count() == 1);
    CHECK(h.undo_label() == "Rename all walls");
    REQUIRE(h.undo(s));
    CHECK(s.to_text() == before);
}

TEST_CASE("history: deleting a subtree and undoing restores it exactly") {
    scene::Scene s = building_scene();
    scene::History h;
    const std::string before = s.to_text();
    const std::size_t count = s.entities.size();
    REQUIRE(h.execute(s, scene::remove_entity(s.entities[0].id)));  // the root: everything goes
    CHECK(s.entities.empty());
    REQUIRE(h.undo(s));
    CHECK(s.entities.size() == count);
    CHECK(s.to_text() == before);
}

TEST_CASE("history: reparenting refuses cycles and keeps parents before children") {
    scene::Scene s = building_scene();
    scene::History h;
    const Guid root = s.entities[0].id;
    const Guid leaf = s.entities.back().id;
    CHECK_FALSE(h.execute(s, scene::reparent(root, leaf)));  // root under its own descendant
    CHECK_FALSE(h.execute(s, scene::reparent(root, root)));
    REQUIRE(h.execute(s, scene::reparent(s.entities[2].id, std::nullopt)));
    CHECK(scene::validate_structure(s).empty());
}

TEST_CASE("history: moving an entity among its siblings keeps its subtree and undoes exactly") {
    // A model with three children; the second has a child of its own.
    scene::Scene s;
    const auto add = [&](const char* name, std::optional<Guid> parent) {
        scene::Entity e;
        e.id = Guid::generate();
        e.name = name;
        e.parent = parent;
        scene::History tmp;
        REQUIRE(tmp.execute(s, scene::add_entity(e)));
        return e.id;
    };
    const Guid model = add("Model", std::nullopt);
    const Guid a = add("A", model), b = add("B", model), c = add("C", model);
    const Guid b_child = add("B child", b);
    const auto children = [&] {
        std::vector<Guid> out;
        for (const auto& e : s.entities)
            if (e.parent == model) out.push_back(e.id);
        return out;
    };
    const auto original = s.entities;
    scene::History h;
    REQUIRE(h.execute(s, scene::move_before(b, a)));  // B first, its child with it
    CHECK(children() == std::vector<Guid>{b, a, c});
    CHECK(scene::validate_structure(s).empty());
    CHECK(s.entities[2].id == b_child);  // right after B
    REQUIRE(h.execute(s, scene::move_before(b, std::nullopt)));  // B last
    CHECK(children() == std::vector<Guid>{a, c, b});
    CHECK(s.entities.back().id == b_child);
    CHECK_FALSE(h.execute(s, scene::move_before(b, std::nullopt)));  // already last: nothing to do
    CHECK_FALSE(h.execute(s, scene::move_before(b, model)));         // not a sibling
    CHECK_FALSE(h.execute(s, scene::move_before(b, b)));
    h.undo(s);
    h.undo(s);
    REQUIRE(s.entities.size() == original.size());
    for (std::size_t i = 0; i < s.entities.size(); ++i) CHECK(s.entities[i].id == original[i].id);
}

TEST_CASE("history: modified() tracks the saved state, including undo back to it") {
    scene::Scene s = building_scene();
    scene::History h;
    h.mark_saved();
    CHECK_FALSE(h.modified());
    REQUIRE(h.execute(s, scene::rename(s.entities[1].id, "Changed")));
    CHECK(h.modified());
    REQUIRE(h.undo(s));
    CHECK_FALSE(h.modified());
}

TEST_CASE("history: random edit sequences undo to the exact original and redo to the exact result") {
    std::mt19937 rng(20261115);
    for (int round = 0; round < 25; ++round) {
        CAPTURE(round);
        scene::Scene s = building_scene();
        scene::History h;
        const std::string original = s.to_text();
        for (int i = 0; i < 40; ++i) {
            if (s.entities.empty()) break;
            const auto pick = [&] { return s.entities[std::uniform_int_distribution<std::size_t>(0, s.entities.size() - 1)(rng)].id; };
            switch (std::uniform_int_distribution<int>(0, 5)(rng)) {
            case 0: {
                scene::Transform t;
                t.position = {static_cast<float>(i), 1.0f, -2.0f};
                h.execute(s, scene::set_transform(pick(), t));
                break;
            }
            case 1: h.execute(s, scene::rename(pick(), "renamed " + std::to_string(i))); break;
            case 2: h.execute(s, scene::set_component(pick(), "test.tag", {{"n", i}})); break;
            case 3: h.execute(s, scene::set_component(pick(), "mesh", nullptr)); break;
            case 4: h.execute(s, scene::reparent(pick(), std::nullopt)); break;
            case 5: {
                scene::Entity e;
                e.id = Guid::generate();
                e.name = "new " + std::to_string(i);
                e.parent = pick();
                h.execute(s, scene::add_entity(e));
                break;
            }
            }
            if (i % 13 == 12 && !s.entities.empty()) h.execute(s, scene::remove_entity(pick()));
        }
        CHECK(scene::validate_structure(s).empty());
        const std::string edited = s.to_text();
        while (h.undo(s)) {
        }
        CHECK(s.to_text() == original);
        while (h.redo(s)) {
        }
        CHECK(s.to_text() == edited);
    }
}

// ---------------------------------------------------------------- migrations

TEST_CASE("migration: a format 1 scene loads, upgrades, and saving keeps the old file as .bak") {
    const char* v1 = R"({
  "noor_scene": 1,
  "id": "11111111-2222-4333-8444-555555555555",
  "name": "Old",
  "entities": [
    {
      "id": "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee",
      "name": "Box",
      "parent": null,
      "transform": {"position": [1, 2, 3], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]},
      "components": {}
    }
  ]
}
)";
    const stdfs::path dir = stdfs::temp_directory_path() / ("noor_migrate_" + Guid::generate().to_string());
    const stdfs::path file = dir / "Old.noorscene";
    REQUIRE(fs::write_file_atomic(file, v1, false));

    auto s = scene::Scene::load(file);
    REQUIRE(s);
    REQUIRE(s->migrations_applied.size() == 1);
    CHECK(s->migrations_applied[0].starts_with("1 -> 2"));
    CHECK(s->entities[0].layer == "default");
    CHECK(s->entities[0].tags.empty());
    CHECK(s->entities[0].transform.position == math::Vec3{1, 2, 3});

    REQUIRE(s->save(file));
    CHECK(*fs::read_file(stdfs::path(file) += ".bak") == v1);  // the backup is the untouched original
    auto again = scene::Scene::load(file);
    REQUIRE(again);
    CHECK(again->migrations_applied.empty());
    CHECK(again->to_text().find("\"noor_scene\": 2") != std::string::npos);
    stdfs::remove_all(dir);
}

TEST_CASE("migration: layer and tags survive a round trip in the current format") {
    scene::Scene s;
    scene::Entity e;
    e.id = Guid::generate();
    e.name = "Tagged";
    e.layer = "props";
    e.tags = {"destructible", "metal"};
    s.entities.push_back(e);
    auto back = scene::Scene::from_text(s.to_text(), "memory");
    REQUIRE(back);
    CHECK(back->entities[0].layer == "props");
    CHECK(back->entities[0].tags == std::vector<std::string>{"destructible", "metal"});
}

// ---------------------------------------------------------------- hot reload

TEST_CASE("file watcher: reports a change once, only after the file stops changing") {
    const stdfs::path dir = stdfs::temp_directory_path() / ("noor_watch_" + Guid::generate().to_string());
    const stdfs::path file = dir / "model.glb";
    REQUIRE(fs::write_file_atomic(file, "version 1", false));
    asset::FileWatcher w(2);
    w.watch(file);
    CHECK(w.poll().empty());

    std::this_thread::sleep_for(20ms);  // make sure the write time can differ
    REQUIRE(fs::write_file_atomic(file, "version 2, longer", false));
    CHECK(w.poll().empty());  // just changed: not settled yet
    const auto settled = w.poll();
    REQUIRE(settled.size() == 1);
    CHECK(settled[0] == file);
    CHECK(w.poll().empty());  // reported once
    stdfs::remove_all(dir);
}

TEST_CASE("file watcher: a file that keeps changing is not reported until it settles") {
    const stdfs::path dir = stdfs::temp_directory_path() / ("noor_watch_" + Guid::generate().to_string());
    const stdfs::path file = dir / "model.glb";
    REQUIRE(fs::write_file_atomic(file, "a", false));
    asset::FileWatcher w(2);
    w.watch(file);
    std::string content = "a";
    for (int i = 0; i < 5; ++i) {
        content += "b";  // grows every poll, like a generator still writing
        REQUIRE(fs::write_file_atomic(file, content, false));
        CHECK(w.poll().empty());  // every poll sees a new size: never settled
    }
    CHECK(w.poll().size() == 1);  // unchanged since the last sighting: settled, reported once
    CHECK(w.poll().empty());
    stdfs::remove_all(dir);
}
