// The procedural graph (Milestone 7, part 1, ADR-014): the file round-trips, every mistake is reported
// on the node it concerns, evaluation is deterministic (pinned fingerprint), node randomness belongs
// to the node, and a node's key changes exactly when something it depends on changes.
#include "noor/foundation/guid.h"
#include "noor/procgraph/procgraph.h"
#include "noor/terrain/terrain.h"

#include <ostream>
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>

using namespace noor;
using namespace noor::procgraph;
using asset::Json;

// Measured 2026-11-18 (Debug and Release, MSVC x64). If it changes, existing graphs make different
// results: change it only on purpose, and bump the versions of the nodes that changed.
#define PROCGRAPH_HILLS_FINGERPRINT 1817818851519113037ull

namespace {

// Hills, flattened in a valley by a mask made from a second noise: the nodes of part 1 working together.
Graph hills(std::uint64_t seed = 7) {
    const Json j = Json::parse(R"({
      "noor_graph": 1, "seed": 7,
      "nodes": [
        {"id": "hills", "type": "noise", "version": 1, "params": {"height": 30, "scale": 120, "octaves": 4}, "position": [0, 0]},
        {"id": "valleys", "type": "noise", "version": 1, "params": {"height": 1, "scale": 300, "octaves": 2}, "position": [0, 200]},
        {"id": "mask", "type": "remap", "version": 1, "params": {"from_min": 0.3, "from_max": 0.7, "unit": "ratio"}, "position": [200, 200]},
        {"id": "flat", "type": "noise", "version": 1, "params": {"height": 2, "scale": 40, "octaves": 1}, "position": [0, 400]},
        {"id": "blend", "type": "combine", "version": 1, "params": {"op": "lerp"}, "position": [400, 100]},
        {"id": "limit", "type": "clamp", "version": 1, "params": {"min": 0, "max": 25}, "position": [600, 100]}
      ],
      "links": [
        {"from": "valleys.height", "to": "mask.in"},
        {"from": "hills.height", "to": "blend.a"},
        {"from": "flat.height", "to": "blend.b"},
        {"from": "mask.out", "to": "blend.t"},
        {"from": "blend.out", "to": "limit.in"}
      ],
      "outputs": [{"name": "terrain", "from": "limit.out"}]
    })");
    auto g = Graph::from_json(j);
    REQUIRE(g);
    g->seed = seed;
    return *g;
}

bool has_issue(const std::vector<Issue>& issues, std::string_view node, std::string_view text) {
    return std::ranges::any_of(issues, [&](const Issue& i) { return i.node == node && i.message.find(text) != std::string::npos; });
}

} // namespace

TEST_CASE("procgraph: a graph file round-trips byte for byte, keeping fields it doesn't know") {
    Graph g = hills();
    g.nodes[0].extra["comment"] = "the big hills";
    g.seed = 13795972169680155491ull;  // above 2^63: must stay an unsigned integer
    const auto dir = std::filesystem::temp_directory_path() / ("noor_graph_" + Guid::generate().to_string());
    std::filesystem::create_directories(dir);
    REQUIRE(g.save(dir / "hills.noorgraph"));
    auto back = Graph::load(dir / "hills.noorgraph");
    REQUIRE(back);
    CHECK(back->seed == 13795972169680155491ull);
    CHECK(back->nodes[0].extra["comment"] == "the big hills");
    CHECK(asset::to_text(back->to_json()) == asset::to_text(g.to_json()));
    CHECK(back->links.size() == 5);
    CHECK(back->outputs[0].from == PinRef{"limit", "out"});
    std::filesystem::remove_all(dir);
    // Files from the future and nonsense are refused with a reason.
    CHECK_FALSE(Graph::from_json(Json::parse(R"({"noor_graph": 2})")));
    CHECK_FALSE(Graph::from_json(Json::parse(R"({"nodes": []})")));
    CHECK_FALSE(Graph::from_json(Json::parse(R"({"noor_graph": 1, "links": [{"from": "nodot", "to": "a.b"}]})")));
}

TEST_CASE("procgraph: a healthy graph evaluates, every node in dependency order") {
    const auto ev = evaluate(hills());
    CHECK(ev.issues.empty());
    REQUIRE(ev.outputs.contains("terrain"));
    const Value& out = ev.outputs.at("terrain");
    CHECK(out.type == PinType::Field);
    CHECK(out.unit == Unit::Metres);
    CHECK(out.field->sx == 257);
    const auto [lo, hi] = std::ranges::minmax(out.field->v);
    CHECK(lo >= 0.0f);
    CHECK(hi <= 25.0f);   // the clamp
    CHECK(hi > 10.0f);    // and real hills
    CHECK(ev.nodes.at("mask").outputs.at("out").unit == Unit::Ratio);
    // Dependencies first; ties by id.
    auto pos = [&](const std::string& id) { return std::ranges::find(ev.order, id) - ev.order.begin(); };
    CHECK(pos("valleys") < pos("mask"));
    CHECK(pos("mask") < pos("blend"));
    CHECK(pos("blend") < pos("limit"));
    CHECK(ev.order.front() == "flat");
    for (const auto& [id, r] : ev.nodes) {
        CHECK(r.state == NodeResult::State::Done);
        CHECK(r.ms >= 0.0);
        CHECK(r.bytes > 0);
        CHECK(r.key != 0);
    }
}

TEST_CASE("procgraph: the same graph gives the same bytes, and node randomness belongs to the node") {
    const auto a = evaluate(hills()), b = evaluate(hills());
    CHECK(a.fingerprint() == b.fingerprint());
    MESSAGE("hills fingerprint: " << a.fingerprint());
    CHECK(a.fingerprint() == PROCGRAPH_HILLS_FINGERPRINT);
    CHECK(evaluate(hills(8)).fingerprint() != a.fingerprint());

    // Adding an unrelated node, or reordering nodes, changes nothing else: seeds come from node ids.
    Graph more = hills();
    more.nodes.insert(more.nodes.begin(), Node{"extra", "noise", 1, Json{{"height", 5}}, {}, Json::object()});
    std::ranges::reverse(more.nodes);
    const auto c = evaluate(more);
    CHECK(c.fingerprint() == a.fingerprint());
    CHECK(c.nodes.at("hills").key == a.nodes.at("hills").key);

    // The noise node is the terrain's noise: the same seed gives the same heights, bit for bit.
    const auto& hf = *a.nodes.at("hills").outputs.at("height").field;
    terrain::Settings s;
    s.tiles_x = s.tiles_z = 1;
    s.base = terrain::Settings::Base::Noise;
    s.height = 30;
    s.scale = 120;
    s.octaves = 4;
    // The node's stream "noise": Random(mix(node seed ^ hash("noise"))), node seed = mix(graph seed ^ hash(id)).
    s.seed = Random(mix(mix(7ull ^ hash_text("hills")) ^ hash_text("noise"))).next();
    auto heights = terrain::Heights::build(s);
    REQUIRE(heights);
    bool same = true;
    for (std::uint32_t z = 0; z < hf.sz; ++z)
        for (std::uint32_t x = 0; x < hf.sx; ++x) same = same && hf.at(x, z) == heights->at(x, z);
    CHECK(same);
}

TEST_CASE("procgraph: a node's key changes when something it depends on does, and only then") {
    const auto base = evaluate(hills());
    Graph g = hills();
    for (auto& n : g.nodes)
        if (n.id == "flat") n.params["height"] = 3;
    const auto changed = evaluate(g);
    // The edited node and everything downstream of it: new keys. The rest: the same.
    for (const char* id : {"flat", "blend", "limit"}) CHECK(changed.nodes.at(id).key != base.nodes.at(id).key);
    for (const char* id : {"hills", "valleys", "mask"}) CHECK(changed.nodes.at(id).key == base.nodes.at(id).key);
    // Parameters in another order are the same parameters.
    Graph reordered = hills();
    reordered.nodes[0].params = Json{{"octaves", 4}, {"scale", 120}, {"height", 30}};
    CHECK(evaluate(reordered).nodes.at("hills").key == base.nodes.at("hills").key);
    // A default written out is the same as a default left out.
    Graph explicit_default = hills();
    explicit_default.nodes[0].params["cell_size"] = 1.0;
    CHECK(evaluate(explicit_default).nodes.at("hills").key == base.nodes.at("hills").key);
}

TEST_CASE("procgraph: mistakes are reported on the node they concern, and the healthy rest still runs") {
    Graph g = hills();
    g.nodes.push_back({"ghost", "teleporter", 1, Json::object(), {}, Json::object()});                     // unknown type
    g.nodes.push_back({"future", "clamp", 2, Json::object(), {}, Json::object()});                         // newer version
    g.nodes.push_back({"lonely", "clamp", 1, Json{{"min", 0}, {"maks", 3}}, {}, Json::object()});          // input missing, typo
    g.nodes.push_back({"loop_a", "clamp", 1, Json::object(), {}, Json::object()});
    g.nodes.push_back({"loop_b", "clamp", 1, Json::object(), {}, Json::object()});
    g.nodes.push_back({"sum", "math", 1, Json{{"op", "power"}}, {}, Json::object()});                      // bad choice
    g.nodes.push_back({"wrong", "combine", 1, Json::object(), {}, Json::object()});
    g.links.push_back({{"loop_a", "out"}, {"loop_b", "in"}});
    g.links.push_back({{"loop_b", "out"}, {"loop_a", "in"}});
    g.links.push_back({{"hills", "height"}, {"sum", "a"}});      // a field into a scalar input
    g.links.push_back({{"hills", "height"}, {"wrong", "a"}});
    g.links.push_back({{"mask", "out"}, {"wrong", "b"}});        // metres and ratio into inputs that must match
    g.links.push_back({{"hills", "height"}, {"wrong", "t"}});    // metres into a ratio input
    g.nodes.push_back({"dup", "clamp", 1, Json::object(), {}, Json::object()});
    g.links.push_back({{"hills", "height"}, {"dup", "in"}});
    g.links.push_back({{"flat", "height"}, {"dup", "in"}});      // a second link into a connected input
    g.links.push_back({{"hills", "nothing"}, {"lonely", "in"}}); // no such output

    const auto issues = validate(g, Registry::builtin());
    CHECK(has_issue(issues, "ghost", "unknown node type \"teleporter\""));
    CHECK(has_issue(issues, "future", "newer version"));
    CHECK(has_issue(issues, "lonely", "unknown parameter \"maks\""));
    CHECK(has_issue(issues, "loop_a", "part of a cycle"));
    CHECK(has_issue(issues, "loop_b", "part of a cycle"));
    CHECK(has_issue(issues, "sum", "must be one of"));
    CHECK(has_issue(issues, "sum", "takes a scalar, but hills.height gives a field"));
    CHECK(has_issue(issues, "sum", "input \"b\" isn't connected"));
    CHECK(has_issue(issues, "wrong", "takes ratio (0..1), but hills.height gives metres"));
    CHECK(has_issue(issues, "dup", "more than one link"));
    CHECK(has_issue(issues, "lonely", "\"hills\" has no output \"nothing\""));  // the receiver's problem, not the source's
    for (const auto& i : issues) MESSAGE(i.node << (i.pin.empty() ? "" : "." + i.pin) << ": " << i.message);

    // The graph still evaluates what it can: the original chain is untouched by its broken neighbours.
    const auto ev = evaluate(g);
    CHECK(ev.outputs.contains("terrain"));
    CHECK(ev.nodes.at("hills").state == NodeResult::State::Done);
    CHECK(ev.nodes.at("ghost").state == NodeResult::State::Failed);
    CHECK(ev.nodes.at("loop_a").state == NodeResult::State::Failed);
    CHECK(ev.nodes.at("wrong").state == NodeResult::State::Failed);
    // remap chooses its unit with a parameter, so "metres + ratio" into `wrong` is only certain while running.
    Graph units = hills();
    units.nodes.push_back({"mixed", "combine", 1, Json::object(), {}, Json::object()});
    units.links.push_back({{"hills", "height"}, {"mixed", "a"}});
    units.links.push_back({{"mask", "out"}, {"mixed", "b"}});
    const auto ue = evaluate(units);
    CHECK(ue.nodes.at("mixed").state == NodeResult::State::Failed);
    CHECK(has_issue(ue.issues, "mixed", "they must match"));
    CHECK(ue.outputs.contains("terrain"));
}

TEST_CASE("procgraph: failures while running stay on their node, and what depends on them is skipped") {
    const Json j = Json::parse(R"({
      "noor_graph": 1, "seed": 1,
      "nodes": [
        {"id": "big", "type": "noise", "params": {"tiles_x": 2}},
        {"id": "small", "type": "noise", "params": {}},
        {"id": "both", "type": "combine", "params": {"op": "add"}},
        {"id": "after", "type": "clamp", "params": {"min": 0, "max": 5}},
        {"id": "two", "type": "constant", "params": {"value": 2, "unit": "metres"}},
        {"id": "three", "type": "constant", "params": {"value": 3, "unit": "metres"}},
        {"id": "five", "type": "math", "params": {"op": "add"}},
        {"id": "half", "type": "constant", "params": {"value": 0.5, "unit": "ratio"}},
        {"id": "odd", "type": "math", "params": {"op": "divide"}}
      ],
      "links": [
        {"from": "big.height", "to": "both.a"}, {"from": "small.height", "to": "both.b"}, {"from": "both.out", "to": "after.in"},
        {"from": "two.value", "to": "five.a"}, {"from": "three.value", "to": "five.b"},
        {"from": "two.value", "to": "odd.a"}, {"from": "half.value", "to": "odd.b"}
      ],
      "outputs": [{"name": "five", "from": "five.out"}]
    })");
    auto g = Graph::from_json(j);
    REQUIRE(g);
    const auto ev = evaluate(*g);
    CHECK(ev.nodes.at("both").state == NodeResult::State::Failed);
    CHECK(ev.nodes.at("both").message.find("different sizes") != std::string::npos);
    CHECK(ev.nodes.at("after").state == NodeResult::State::Skipped);
    CHECK(ev.nodes.at("five").outputs.at("out").scalar == 5.0);
    CHECK(ev.nodes.at("five").outputs.at("out").unit == Unit::Metres);
    // Metres and a ratio only meet while running (constants choose their unit): caught then.
    CHECK(ev.nodes.at("odd").state == NodeResult::State::Failed);
    CHECK(ev.nodes.at("odd").message.find("they must match") != std::string::npos);
    CHECK(ev.outputs.at("five").scalar == 5.0);
}
