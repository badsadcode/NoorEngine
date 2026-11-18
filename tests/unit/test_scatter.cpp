// Milestone 7, part 7: scatter. The `scatter`, `exclude` and `merge` nodes, the instance set file, and
// the scatter component's hand-made layer surviving regeneration with another seed (the gate's
// "a local override survives regeneration", for instances).

#include "noor/asset/instance_set.h"
#include "noor/asset/json.h"
#include "noor/game/game.h"
#include "noor/game/scatter.h"
#include "noor/procgraph/procgraph.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <set>
#include <filesystem>
#include <ostream>
#include <doctest/doctest.h>

namespace stdfs = std::filesystem;
using namespace noor;
using asset::Json;

namespace {

constexpr const char* kOak = "719beb3e-327d-56ed-8d54-be8882f405fd";
const Guid kGraph = *Guid::parse("7e770000-5ca7-4000-8000-000000000001");

// A small forest: oaks over 200 x 200 m, kept off a path and out of a clearing.
procgraph::Graph forest(std::uint64_t seed, double density = 1.0, double spacing = 8.0) {
    const Json j = Json::parse(std::format(R"({{
        "noor_graph": 1, "seed": {},
        "nodes": [
          {{"id": "clear", "type": "exclude", "params": {{"tiles_x": 1, "tiles_z": 1, "cell_size": 1, "paths": "-100,0 100,0", "width": 8,
                                                         "circles": "40,40,15", "soft": 0}}}},
          {{"id": "trees", "type": "scatter", "params": {{"asset": "{}", "density": {}, "spacing": {}, "width": 200, "depth": 200}}}}
        ],
        "links": [{{"from": "clear.mask", "to": "trees.mask"}}],
        "outputs": [{{"name": "trees", "from": "trees.instances"}}] }})",
                                           seed, kOak, density, spacing));
    return *procgraph::Graph::from_json(j);
}

const std::vector<procgraph::Instance>& trees(const procgraph::Evaluation& ev) {
    REQUIRE(ev.outputs.contains("trees"));
    return *ev.outputs.at("trees").instances;
}

} // namespace

TEST_CASE("scatter: deterministic, spaced, masked, every tree with its own id") {
    const auto a = procgraph::evaluate(forest(7));
    const auto b = procgraph::evaluate(forest(7));
    for (const auto& i : a.issues) INFO(i.node, ": ", i.message);
    REQUIRE(a.issues.empty());
    const auto& t = trees(a);
    CHECK(a.fingerprint() == b.fingerprint());
    // Asked for 1 per 100 m² over 3.8 ha (4 ha minus the path and the clearing): 377. Spacing 8 m is near
    // the limit random placement reaches (about 1.1 per 100 m² at that spacing), so crowded candidates
    // are dropped and about 55% remain. Far fewer would mean the mask or the thinning broke.
    CHECK(t.size() > 150);
    CHECK(t.size() < 377);
    // Well under the limit, the density is close to what was asked: 0.2 per 100 m² -> about 75.
    const auto sparse = procgraph::evaluate(forest(7, 0.2, 8.0));
    CHECK(trees(sparse).size() > 55);
    CHECK(trees(sparse).size() < 90);
    std::set<std::uint64_t> ids;
    double closest = 1e9;
    for (std::size_t i = 0; i < t.size(); ++i) {
        ids.insert(t[i].seed);
        CHECK(t[i].asset.to_string() == kOak);
        // Off the path (|z| < 4) and out of the clearing (15 m around (40, 40)).
        CHECK(std::abs(t[i].position.z) >= 4.0f);
        CHECK(std::hypot(t[i].position.x - 40.0f, t[i].position.z - 40.0f) >= 15.0f);
        const auto& q = t[i].rotation;
        CHECK(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w == doctest::Approx(1.0f).epsilon(1e-5));
        CHECK(t[i].scale >= 0.85f);
        CHECK(t[i].scale <= 1.15f);
        for (std::size_t k = i + 1; k < t.size(); ++k)
            closest = std::min(closest, static_cast<double>(std::hypot(t[i].position.x - t[k].position.x, t[i].position.z - t[k].position.z)));
    }
    CHECK(ids.size() == t.size());
    CHECK(closest >= 8.0);
    // Another seed: another forest.
    CHECK(procgraph::evaluate(forest(8)).fingerprint() != a.fingerprint());
}

TEST_CASE("scatter: merge keeps b's copies away from a's") {
    const Json j = Json::parse(std::format(R"({{
        "noor_graph": 1, "seed": 3,
        "nodes": [
          {{"id": "oaks", "type": "scatter", "params": {{"asset": "{}", "density": 1, "spacing": 10, "width": 100, "depth": 100}}}},
          {{"id": "rocks", "type": "scatter", "params": {{"asset": "{}", "density": 4, "spacing": 3, "width": 100, "depth": 100}}}},
          {{"id": "all", "type": "merge", "params": {{"spacing": 4}}}}
        ],
        "links": [{{"from": "oaks.instances", "to": "all.a"}}, {{"from": "rocks.instances", "to": "all.b"}}],
        "outputs": [{{"name": "all", "from": "all.instances"}}] }})",
                                           kOak, "7e770000-0c4c-4000-8000-000000000002"));
    const auto ev = procgraph::evaluate(*procgraph::Graph::from_json(j));
    REQUIRE(ev.issues.empty());
    const auto& oaks = *ev.nodes.at("oaks").outputs.at("instances").instances;
    const auto& rocks = *ev.nodes.at("rocks").outputs.at("instances").instances;
    const auto& all = *ev.outputs.at("all").instances;
    CHECK(all.size() > oaks.size());
    CHECK(all.size() < oaks.size() + rocks.size());  // some rocks were too close to an oak
    for (std::size_t i = oaks.size(); i < all.size(); ++i)
        for (const auto& o : oaks) CHECK(std::hypot(all[i].position.x - o.position.x, all[i].position.z - o.position.z) >= 4.0f);
}

TEST_CASE("scatter: mistakes are issues on the node") {
    auto g = forest(1);
    g.nodes[1].params["asset"] = "not an id";
    const auto ev = procgraph::evaluate(g);
    REQUIRE_FALSE(ev.outputs.contains("trees"));
    bool found = false;
    for (const auto& i : ev.issues) found = found || (i.node == "trees" && i.message.find("asset") != std::string::npos);
    CHECK(found);
    auto bad_path = forest(1);
    bad_path.nodes[0].params["paths"] = "1,2 3";
    const auto ev2 = procgraph::evaluate(bad_path);
    bool found2 = false;
    for (const auto& i : ev2.issues) found2 = found2 || i.node == "clear";
    CHECK(found2);
}

TEST_CASE("instance sets: the file reads back bit for bit, and mistakes are errors") {
    const auto ev = procgraph::evaluate(forest(5));
    const auto set = procgraph::to_instance_set(trees(ev));
    const auto text = set.to_text();
    auto back = asset::InstanceSet::from_text(text, "forest");
    REQUIRE(back);
    REQUIRE(back->items.size() == set.items.size());
    REQUIRE(back->assets == set.assets);
    for (std::size_t i = 0; i < set.items.size(); ++i) {
        CHECK(back->items[i].id == set.items[i].id);
        CHECK(back->items[i].position == set.items[i].position);  // exact: every bit
        CHECK(back->items[i].rotation.y == set.items[i].rotation.y);
        CHECK(back->items[i].scale == set.items[i].scale);
    }
    CHECK(back->to_text() == text);
    CHECK_FALSE(asset::InstanceSet::from_text(R"({"assets": []})", "x"));
    CHECK_FALSE(asset::InstanceSet::from_text(R"({"noor_instances": 1, "assets": [], "instances": [[0, "1", 0,0,0, 0,0,0,1, 1]]})", "x"));
    CHECK_FALSE(asset::InstanceSet::from_text(R"({"noor_instances": 2, "assets": [], "instances": []})", "x"));
    CHECK(asset::parse_instance_id("9f3c0a1b2c3d4e5f") == 0x9f3c0a1b2c3d4e5full);
    CHECK_FALSE(asset::parse_instance_id("xyz"));
}

TEST_CASE("scatter component: trees removed and placed by hand survive a new seed") {
    const auto root = stdfs::temp_directory_path() / ("noor_scatter_" + Guid::generate().to_string());
    const auto cook = [&](std::uint64_t seed) {
        procgraph::GenerateOptions o;
        o.out = game::cooked_graph_folder(root, kGraph);
        o.name = "graph";
        auto r = procgraph::generate(forest(seed), procgraph::Registry::builtin(), o);
        REQUIRE(r);
    };
    cook(1);
    Json comp{{"graph", kGraph.to_string()}, {"output", "trees"}, {"removed", Json::array()}, {"added", Json::array()}};
    const auto first = game::scatter_items(comp, root);
    REQUIRE(first.size() > 100);
    const std::uint64_t gone = first[10].id;
    comp["removed"].push_back(asset::instance_id_text(gone));
    comp["added"].push_back({{"id", asset::instance_id_text(game::new_instance_id())}, {"asset", kOak}, {"position", {1.5, 0, -2.5}}, {"yaw", 90}, {"scale", 1.2}});
    auto edited = game::scatter_items(comp, root);
    CHECK(edited.size() == first.size());  // one out, one in
    CHECK(std::ranges::none_of(edited, [&](const auto& i) { return i.id == gone; }));
    const auto placed = std::ranges::find_if(edited, [](const auto& i) { return i.added; });
    REQUIRE(placed != edited.end());
    CHECK(placed->position.x == doctest::Approx(1.5f));
    CHECK(placed->scale == doctest::Approx(1.2f));

    // Regenerate with another seed: a different forest, the hand-placed tree still there.
    cook(2);
    const auto reseeded = game::scatter_items(comp, root);
    CHECK(std::ranges::count_if(reseeded, [](const auto& i) { return i.added; }) == 1);
    CHECK(std::ranges::none_of(reseeded, [&](const auto& i) { return i.id == gone; }));
    CHECK(reseeded.size() != edited.size());
    // And back: the removed tree is still removed.
    cook(1);
    const auto back = game::scatter_items(comp, root);
    CHECK(back.size() == edited.size());
    CHECK(std::ranges::none_of(back, [&](const auto& i) { return i.id == gone; }));

    // Not cooked: only the hand-placed ones, and the reason.
    std::string why;
    Json other = comp;
    other["graph"] = Guid::generate().to_string();
    CHECK(game::scatter_items(other, root, &why).size() == 1);
    CHECK_FALSE(why.empty());
    std::error_code ec;
    stdfs::remove_all(root, ec);
}

TEST_CASE("scatter component: the graphs a scene needs cooked") {
    scene::Scene s;
    scene::Entity t, f, g;
    t.id = Guid::generate();
    t.components["terrain"] = {{"base", "graph"}, {"graph", kGraph.to_string()}};
    f.id = Guid::generate();
    f.components["scatter"] = {{"graph", kGraph.to_string()}, {"output", "trees"}};
    g.id = Guid::generate();
    g.components["scatter"] = {{"graph", "7e770000-5ca7-4000-8000-000000000009"}};
    s.entities = {t, f, g};
    const auto used = game::graphs_used(s);
    REQUIRE(used.size() == 2);
    CHECK(used[0] == kGraph);
}
