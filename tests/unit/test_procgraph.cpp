// The procedural graph (Milestone 7, part 1, ADR-014): the file round-trips, every mistake is reported
// on the node it concerns, evaluation is deterministic (pinned fingerprint), node randomness belongs
// to the node, and a node's key changes exactly when something it depends on changes.
#include "noor/foundation/file_io.h"
#include "noor/foundation/guid.h"
#include "noor/procgraph/procgraph.h"
#include "noor/terrain/terrain.h"

#include <ostream>
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <format>
#include <fstream>

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

// ---- Milestone 7, part 2: cache, threads, cancel, generate ----

namespace {
std::filesystem::path temp_folder(const char* what) {
    auto dir = std::filesystem::temp_directory_path() / (std::string(what) + "_" + Guid::generate().to_string());
    std::filesystem::create_directories(dir);
    return dir;
}
std::size_t count_files(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return 0;
    return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator(dir), std::filesystem::directory_iterator()));
}
} // namespace

TEST_CASE("procgraph: values round-trip through bytes, every bit") {
    const auto ev = evaluate(hills());
    for (const auto& [id, r] : ev.nodes)
        for (const auto& [name, v] : r.outputs) {
            auto back = decode(encode(v));
            REQUIRE(back);
            CHECK(back->hash() == v.hash());
        }
    Value seeds;
    seeds.type = PinType::Seed;
    seeds.seed = 18446744073709551615ull;
    CHECK(decode(encode(seeds))->seed == 18446744073709551615ull);
    CHECK_FALSE(decode("nonsense"));
}

TEST_CASE("procgraph: only changed nodes and what depends on them run again, from memory or from files") {
    const auto dir = temp_folder("noor_pgcache");
    Cache cache(dir);
    EvaluateOptions opts;
    opts.cache = &cache;
    const auto first = evaluate(hills(), Registry::builtin(), opts);
    CHECK(first.ran == 6);
    CHECK(first.cached == 0);
    CHECK(count_files(dir) == 6);
    const auto second = evaluate(hills(), Registry::builtin(), opts);
    CHECK(second.ran == 0);
    CHECK(second.cached == 6);
    CHECK(second.fingerprint() == first.fingerprint());

    // One parameter changed: that node and its two dependants run; the other three come from the cache.
    Graph g = hills();
    for (auto& n : g.nodes)
        if (n.id == "flat") n.params["height"] = 3;
    const auto edited = evaluate(g, Registry::builtin(), opts);
    CHECK(edited.ran == 3);
    CHECK(edited.cached == 3);
    for (const char* id : {"flat", "blend", "limit"}) CHECK_FALSE(edited.nodes.at(id).cached);
    for (const char* id : {"hills", "valleys", "mask"}) CHECK(edited.nodes.at(id).cached);

    // A new process (memory empty): the files serve it, with the same bytes as computing it.
    cache.forget_memory();
    const auto from_files = evaluate(hills(), Registry::builtin(), opts);
    CHECK(from_files.cached == 6);
    CHECK(cache.file_hits() == 6);
    CHECK(from_files.fingerprint() == first.fingerprint());

    // A damaged cache file is a miss: computed again (same result) and replaced.
    cache.forget_memory();
    const auto victim = dir / std::format("{:016x}.npgc", first.nodes.at("limit").key);
    REQUIRE(std::filesystem::exists(victim));
    { std::ofstream(victim, std::ios::binary | std::ios::trunc) << "NPGCACHE garbage"; }
    const auto repaired = evaluate(hills(), Registry::builtin(), opts);
    CHECK_FALSE(repaired.nodes.at("limit").cached);
    CHECK(repaired.fingerprint() == first.fingerprint());
    std::filesystem::remove_all(dir);
}

TEST_CASE("procgraph: threads change nothing: one thread and many give the same bytes and order") {
    EvaluateOptions one, many;
    one.threads = 1;
    many.threads = 8;
    std::size_t progress_calls = 0;
    many.progress = [&](const Progress& p) {
        ++progress_calls;
        CHECK(p.total == 6);
    };
    const auto a = evaluate(hills(), Registry::builtin(), one), b = evaluate(hills(), Registry::builtin(), many);
    CHECK(a.fingerprint() == b.fingerprint());
    CHECK(a.order == b.order);
    CHECK(progress_calls == 6);
    for (const auto& [id, r] : a.nodes) CHECK(r.key == b.nodes.at(id).key);
}

TEST_CASE("procgraph: cancelling stops the run, keeps no half results, and generate writes nothing") {
    const auto cache_dir = temp_folder("noor_pgcancel");
    const auto out = temp_folder("noor_pgout");
    Cache cache(cache_dir);
    std::atomic<bool> cancel{false};
    GenerateOptions opts;
    opts.out = out;
    opts.name = "hills";
    opts.run.cache = &cache;
    opts.run.threads = 1;
    opts.run.cancel = &cancel;
    opts.run.progress = [&](const Progress& p) {
        if (p.done == 2) cancel = true;  // stop after two nodes
    };
    auto r = generate(hills(), Registry::builtin(), opts);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == ErrorCode::Cancelled);
    CHECK(count_files(out) == 0);         // nothing half-written in the output folder
    CHECK(count_files(cache_dir) == 2);   // only the two finished nodes were cached

    // The next run picks up where it stopped: those two from the cache, the rest computed.
    cancel = false;
    opts.run.progress = nullptr;
    auto done = generate(hills(), Registry::builtin(), opts);
    REQUIRE(done);
    CHECK(done->evaluation.cached == 2);
    CHECK(done->evaluation.ran == 4);
    CHECK(done->fingerprint == PROCGRAPH_HILLS_FINGERPRINT);
    std::filesystem::remove_all(cache_dir);
    std::filesystem::remove_all(out);
}

TEST_CASE("procgraph: generate writes the same bytes every time, files another tool can read") {
    const auto a = temp_folder("noor_pgen_a"), b = temp_folder("noor_pgen_b");
    GenerateOptions opts;
    opts.name = "hills";
    opts.out = a;
    auto first = generate(hills(), Registry::builtin(), opts);  // no cache
    REQUIRE(first);
    Cache cache;
    opts.out = b;
    opts.run.cache = &cache;
    opts.run.threads = 4;
    auto second = generate(hills(), Registry::builtin(), opts);
    REQUIRE(second);
    REQUIRE(first->files.size() == 2);  // terrain.noorfield, hills.fingerprint.json
    for (std::size_t i = 0; i < first->files.size(); ++i) {
        CHECK(first->files[i].filename() == second->files[i].filename());
        CHECK(fs::read_file(first->files[i], 1u << 28).value() == fs::read_file(second->files[i], 1u << 28).value());
    }
    // The field file: a plain format with its unit, readable without the graph.
    auto field = load_field(a / "terrain.noorfield");
    REQUIRE(field);
    CHECK(field->second == Unit::Metres);
    CHECK(field->first.sx == 257);
    CHECK(field->first.v == first->evaluation.outputs.at("terrain").field->v);
    // The fingerprint file says what made it.
    const auto fp = asset::parse_json(fs::read_file(a / "hills.fingerprint.json").value(), "fp");
    REQUIRE(fp);
    CHECK((*fp)["fingerprint"] == std::format("{:016x}", PROCGRAPH_HILLS_FINGERPRINT));
    CHECK((*fp)["nodes"]["hills"]["type"] == "noise");
    CHECK((*fp)["outputs"]["terrain"]["file"] == "terrain.noorfield");
    // A graph whose output can't be made writes nothing.
    Graph broken = hills();
    broken.nodes.push_back({"oops", "clamp", 1, Json::object(), {}, Json::object()});
    broken.outputs.push_back({"bad", {"oops", "out"}});
    const auto c = temp_folder("noor_pgen_c");
    opts.out = c;
    CHECK_FALSE(generate(broken, Registry::builtin(), opts));
    CHECK(count_files(c) == 0);
    for (const auto& d : {a, b, c}) std::filesystem::remove_all(d);
}
