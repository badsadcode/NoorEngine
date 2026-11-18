#include "noor/procgraph/procgraph.h"

#include "noor/foundation/file_io.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace noor::procgraph {
using asset::Json;

// ---- Names ----

std::string_view type_name(PinType type) {
    switch (type) {
    case PinType::Scalar: return "scalar";
    case PinType::Vector: return "vector";
    case PinType::Seed: return "seed";
    case PinType::Field: return "field";
    case PinType::InstanceSet: return "instance set";
    case PinType::AssetRef: return "asset";
    }
    return "?";
}

std::string_view unit_name(Unit unit) {
    switch (unit) {
    case Unit::None: return "none";
    case Unit::Metres: return "metres";
    case Unit::Ratio: return "ratio (0..1)";
    case Unit::Degrees: return "degrees";
    case Unit::Any: return "any";
    case Unit::SameAs: return "same as an input";
    }
    return "?";
}

// ---- Hashing and randomness ----

namespace {

struct Hasher {
    std::uint64_t h = 1469598103934665603ull;
    void byte(std::uint8_t b) noexcept {
        h ^= b;
        h *= 1099511628211ull;
    }
    void u64(std::uint64_t v) noexcept {
        for (int i = 0; i < 8; ++i) byte(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void u32(std::uint32_t v) noexcept {
        for (int i = 0; i < 4; ++i) byte(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void f32(float v) noexcept { u32(std::bit_cast<std::uint32_t>(v)); }
    void f64(double v) noexcept { u64(std::bit_cast<std::uint64_t>(v)); }
    void text(std::string_view s) noexcept {
        u64(s.size());
        for (const char c : s) byte(static_cast<std::uint8_t>(c));
    }
};

} // namespace

std::uint64_t mix(std::uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

std::uint64_t hash_text(std::string_view text) noexcept {
    Hasher h;
    for (const char c : text) h.byte(static_cast<std::uint8_t>(c));
    return h.h;
}

std::uint64_t Random::next() noexcept {
    state_ += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

double Random::uniform() noexcept { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }

std::uint64_t Value::hash() const noexcept {
    Hasher h;
    h.byte(static_cast<std::uint8_t>(type));
    h.byte(static_cast<std::uint8_t>(unit));
    switch (type) {
    case PinType::Scalar: h.f64(scalar); break;
    case PinType::Vector:
        h.f32(vector.x);
        h.f32(vector.y);
        h.f32(vector.z);
        break;
    case PinType::Seed: h.u64(seed); break;
    case PinType::Field:
        if (field) {
            h.u32(field->sx);
            h.u32(field->sz);
            h.f32(field->cell);
            for (const float f : field->v) h.f32(f);
        }
        break;
    case PinType::InstanceSet:
        if (instances)
            for (const auto& i : *instances) {
                h.text(i.asset.to_string());
                h.f32(i.position.x), h.f32(i.position.y), h.f32(i.position.z);
                h.f32(i.rotation.x), h.f32(i.rotation.y), h.f32(i.rotation.z), h.f32(i.rotation.w);
                h.f32(i.scale);
                h.u64(i.seed);
            }
        break;
    case PinType::AssetRef: h.text(asset.to_string()); break;
    }
    return h.h;
}

namespace {

std::uint64_t value_bytes(const Value& v) {
    std::uint64_t n = sizeof(Value);
    if (v.field) n += v.field->v.size() * sizeof(float);
    if (v.instances) n += v.instances->size() * sizeof(Instance);
    return n;
}

} // namespace

// ---- The graph file ----

namespace {

Result<PinRef> parse_ref(const Json& j, std::string_view what) {
    if (!j.is_string()) return fail(ErrorCode::ParseError, std::format("{} must be \"node.pin\"", what));
    const std::string s = j.get<std::string>();
    const auto dot = s.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == s.size())
        return fail(ErrorCode::ParseError, std::format("{} \"{}\" must be \"node.pin\"", what, s));
    return PinRef{s.substr(0, dot), s.substr(dot + 1)};
}

} // namespace

Result<Graph> Graph::from_json(const Json& json) {
    if (!json.is_object() || !json.contains("noor_graph")) return fail(ErrorCode::ParseError, "not a .noorgraph file (no \"noor_graph\")");
    if (!json["noor_graph"].is_number_integer() || json["noor_graph"].get<int>() > kFormat)
        return fail(ErrorCode::ParseError, std::format("graph format {} is newer than this engine reads ({})", json["noor_graph"].dump(), kFormat));
    Graph g;
    if (json.contains("seed")) {
        if (json["seed"].is_number_unsigned()) g.seed = json["seed"].get<std::uint64_t>();
        else if (json["seed"].is_number_integer() && json["seed"].get<std::int64_t>() >= 0) g.seed = static_cast<std::uint64_t>(json["seed"].get<std::int64_t>());
        else return fail(ErrorCode::ParseError, "\"seed\" must be an unsigned 64-bit integer");
    }
    for (const auto& n : json.value("nodes", Json::array())) {
        if (!n.is_object() || !n.contains("id") || !n["id"].is_string() || !n.contains("type") || !n["type"].is_string())
            return fail(ErrorCode::ParseError, "every node needs a text \"id\" and \"type\"");
        Node node;
        node.id = n["id"].get<std::string>();
        node.type = n["type"].get<std::string>();
        if (n.contains("version") && n["version"].is_number_unsigned()) node.version = n["version"].get<std::uint32_t>();
        if (n.contains("params") && n["params"].is_object()) node.params = n["params"];
        if (n.contains("position") && n["position"].is_array() && n["position"].size() == 2 && n["position"][0].is_number() && n["position"][1].is_number())
            node.position = {n["position"][0].get<float>(), n["position"][1].get<float>()};
        for (auto it = n.begin(); it != n.end(); ++it)
            if (it.key() != "id" && it.key() != "type" && it.key() != "version" && it.key() != "params" && it.key() != "position") node.extra[it.key()] = it.value();
        g.nodes.push_back(std::move(node));
    }
    for (const auto& l : json.value("links", Json::array())) {
        if (!l.is_object()) return fail(ErrorCode::ParseError, "links are objects {\"from\", \"to\"}");
        auto from = parse_ref(l.value("from", Json()), "a link's \"from\"");
        if (!from) return std::unexpected(from.error());
        auto to = parse_ref(l.value("to", Json()), "a link's \"to\"");
        if (!to) return std::unexpected(to.error());
        g.links.push_back({*from, *to});
    }
    for (const auto& o : json.value("outputs", Json::array())) {
        if (!o.is_object() || !o.contains("name") || !o["name"].is_string()) return fail(ErrorCode::ParseError, "outputs are objects {\"name\", \"from\"}");
        auto from = parse_ref(o.value("from", Json()), "an output's \"from\"");
        if (!from) return std::unexpected(from.error());
        g.outputs.push_back({o["name"].get<std::string>(), *from});
    }
    return g;
}

Json Graph::to_json() const {
    Json j;
    j["noor_graph"] = kFormat;
    j["seed"] = seed;
    Json nodes_json = Json::array();
    for (const auto& n : nodes) {
        Json nj;
        nj["id"] = n.id;
        nj["type"] = n.type;
        nj["version"] = n.version;
        nj["params"] = n.params;
        nj["position"] = {n.position.x, n.position.y};
        for (auto it = n.extra.begin(); it != n.extra.end(); ++it) nj[it.key()] = it.value();
        nodes_json.push_back(std::move(nj));
    }
    j["nodes"] = std::move(nodes_json);
    Json links_json = Json::array();
    for (const auto& l : links) links_json.push_back({{"from", l.from.text()}, {"to", l.to.text()}});
    j["links"] = std::move(links_json);
    Json outputs_json = Json::array();
    for (const auto& o : outputs) outputs_json.push_back({{"name", o.name}, {"from", o.from.text()}});
    j["outputs"] = std::move(outputs_json);
    return j;
}

Result<Graph> Graph::load(const std::filesystem::path& file) {
    auto text = fs::read_file(file, 64ull << 20);
    if (!text) return std::unexpected(text.error());
    auto json = asset::parse_json(*text, file.generic_string());
    if (!json) return std::unexpected(json.error());
    return from_json(*json);
}

Status Graph::save(const std::filesystem::path& file) const { return fs::write_file_atomic(file, asset::to_text(to_json()), true); }

const Node* Graph::find(std::string_view id) const {
    for (const auto& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

// ---- Registry ----

void Registry::add(NodeType type) {
    const std::string name = type.name;
    types_[name] = std::move(type);
}

const NodeType* Registry::find(std::string_view name) const {
    const auto it = types_.find(name);
    return it == types_.end() ? nullptr : &it->second;
}

// ---- Checking ----

namespace {

const PinSpec* find_pin(const std::vector<PinSpec>& pins, std::string_view name) {
    for (const auto& p : pins)
        if (p.name == name) return &p;
    return nullptr;
}

// A node's parameters with defaults filled in and ranges applied; problems are reported, not fatal.
Json fill_params(const NodeType& type, const Node& node, std::vector<Issue>* issues) {
    Json out = Json::object();
    for (const auto& p : type.params) {
        const bool has = node.params.contains(p.name);
        const Json& given = has ? node.params[p.name] : p.fallback;
        auto report = [&](const std::string& why) {
            if (issues) issues->push_back({node.id, "", std::format("parameter \"{}\": {}", p.name, why)});
        };
        switch (p.kind) {
        case ParamSpec::Kind::Number:
        case ParamSpec::Kind::Integer:
            if (!given.is_number()) {
                report("must be a number");
                out[p.name] = p.fallback;
                break;
            }
            {
                double v = std::clamp(given.get<double>(), p.min, p.max);
                if (p.kind == ParamSpec::Kind::Integer) {
                    v = std::floor(v + 0.5);
                    out[p.name] = static_cast<std::int64_t>(v);
                } else {
                    out[p.name] = v;
                }
            }
            break;
        case ParamSpec::Kind::Choice:
            if (!given.is_string() || std::ranges::find(p.choices, given.get<std::string>()) == p.choices.end()) {
                std::string list;
                for (const auto& c : p.choices) list += (list.empty() ? "" : ", ") + c;
                report(std::format("must be one of {}", list));
                out[p.name] = p.fallback;
            } else {
                out[p.name] = given;
            }
            break;
        case ParamSpec::Kind::Text:
            out[p.name] = given.is_string() ? given : p.fallback;
            if (!given.is_string()) report("must be text");
            break;
        case ParamSpec::Kind::Flag:
            out[p.name] = given.is_boolean() ? given : p.fallback;
            if (!given.is_boolean()) report("must be true or false");
            break;
        }
    }
    if (issues)
        for (auto it = node.params.begin(); it != node.params.end(); ++it)
            if (std::ranges::none_of(type.params, [&](const ParamSpec& p) { return p.name == it.key(); }))
                issues->push_back({node.id, "", std::format("unknown parameter \"{}\"", it.key())});
    return out;
}

struct Index {
    const Graph& g;
    const Registry& r;
    std::unordered_map<std::string, const Node*> nodes;
    std::unordered_map<std::string, const NodeType*> types;
    std::map<std::pair<std::string, std::string>, const Link*> into;  // (node, input) -> its link (the first)
    Index(const Graph& graph, const Registry& registry) : g(graph), r(registry) {
        for (const auto& n : g.nodes) {
            nodes.try_emplace(n.id, &n);
            if (const auto* t = r.find(n.type)) types.try_emplace(n.id, t);
        }
        for (const auto& l : g.links) into.try_emplace({l.to.node, l.to.pin}, &l);
    }
    // The unit an output really carries (following SameAs back through the graph); Any if unknown.
    Unit output_unit(const std::string& node, const std::string& pin, int depth = 0) const {
        const auto t = types.find(node);
        if (t == types.end() || depth > 256) return Unit::Any;
        const PinSpec* spec = find_pin(t->second->outputs, pin);
        if (!spec) return Unit::Any;
        if (spec->unit != Unit::SameAs) return spec->unit;
        const auto link = into.find({node, spec->same_as});
        return link == into.end() ? Unit::Any : output_unit(link->second->from.node, link->second->from.pin, depth + 1);
    }
};

} // namespace

std::vector<Issue> validate(const Graph& graph, const Registry& registry) {
    std::vector<Issue> issues;
    const Index ix(graph, registry);
    std::unordered_set<std::string> seen;
    for (const auto& n : graph.nodes) {
        if (!seen.insert(n.id).second) issues.push_back({n.id, "", "another node has the same id"});
        if (n.id.empty() || n.id.find('.') != std::string::npos) issues.push_back({n.id, "", "a node id can't be empty or contain '.'"});
        const NodeType* t = registry.find(n.type);
        if (!t) {
            issues.push_back({n.id, "", std::format("unknown node type \"{}\"", n.type)});
            continue;
        }
        if (n.version > t->version)
            issues.push_back({n.id, "", std::format("made with a newer version of \"{}\" ({}) than this engine has ({})", n.type, n.version, t->version)});
        else if (n.version < t->version)
            issues.push_back({n.id, "", std::format("made with version {} of \"{}\"; this engine has version {} and no migration for it", n.version, n.type, t->version)});
        (void)fill_params(*t, n, &issues);
        for (const auto& in : t->inputs)
            if (!in.optional && !ix.into.contains({n.id, in.name})) issues.push_back({n.id, in.name, std::format("input \"{}\" isn't connected", in.name)});
        // Inputs that must share a unit.
        std::optional<Unit> shared;
        std::string first;
        for (const auto& name : t->same_units) {
            const auto link = ix.into.find({n.id, name});
            if (link == ix.into.end()) continue;
            const Unit u = ix.output_unit(link->second->from.node, link->second->from.pin);
            if (u == Unit::Any) continue;
            if (!shared) {
                shared = u;
                first = name;
            } else if (*shared != u) {
                issues.push_back({n.id, name, std::format("\"{}\" is in {} but \"{}\" is in {}: they must match", name, unit_name(u), first, unit_name(*shared))});
            }
        }
    }
    std::set<std::pair<std::string, std::string>> fed;
    for (const auto& l : graph.links) {
        const auto from_type = ix.types.find(l.from.node);
        const auto to_type = ix.types.find(l.to.node);
        if (!ix.nodes.contains(l.from.node)) {
            issues.push_back({l.to.node, l.to.pin, std::format("linked from a node that doesn't exist (\"{}\")", l.from.node)});
            continue;
        }
        if (!ix.nodes.contains(l.to.node)) {
            // The graph's problem, not the healthy source node's.
            issues.push_back({"", "", std::format("a link from {} goes to a node that doesn't exist (\"{}\")", l.from.text(), l.to.node)});
            continue;
        }
        if (from_type == ix.types.end() || to_type == ix.types.end()) continue;  // unknown types: reported above
        const PinSpec* out = find_pin(from_type->second->outputs, l.from.pin);
        const PinSpec* in = find_pin(to_type->second->inputs, l.to.pin);
        if (!out) {  // the receiving node's problem: its input is linked to nothing real
            issues.push_back({l.to.node, l.to.pin, std::format("input \"{}\" is linked from {}, but \"{}\" has no output \"{}\"", l.to.pin, l.from.text(), l.from.node, l.from.pin)});
            continue;
        }
        if (!in) {
            issues.push_back({l.to.node, l.to.pin, std::format("\"{}\" has no input \"{}\"", l.to.node, l.to.pin)});
            continue;
        }
        if (!fed.insert({l.to.node, l.to.pin}).second) {
            issues.push_back({l.to.node, l.to.pin, std::format("input \"{}\" has more than one link", l.to.pin)});
            continue;
        }
        if (out->type != in->type) {
            issues.push_back({l.to.node, l.to.pin, std::format("input \"{}\" takes a {}, but {} gives a {}", l.to.pin, type_name(in->type), l.from.text(), type_name(out->type))});
            continue;
        }
        const Unit u = ix.output_unit(l.from.node, l.from.pin);
        if (in->unit != Unit::Any && u != Unit::Any && u != in->unit)
            issues.push_back({l.to.node, l.to.pin, std::format("input \"{}\" takes {}, but {} gives {}", l.to.pin, unit_name(in->unit), l.from.text(), unit_name(u))});
    }
    // Cycles: every node on one is told which.
    std::unordered_map<std::string, std::vector<std::string>> next;
    for (const auto& l : graph.links)
        if (ix.nodes.contains(l.from.node) && ix.nodes.contains(l.to.node)) next[l.from.node].push_back(l.to.node);
    std::unordered_map<std::string, int> state;  // 0 new, 1 on the path, 2 done
    std::vector<std::string> path;
    std::set<std::string> in_cycle;
    std::function<void(const std::string&)> visit = [&](const std::string& id) {
        state[id] = 1;
        path.push_back(id);
        for (const auto& to : next[id]) {
            if (state[to] == 1) {
                const auto start = std::ranges::find(path, to);
                std::string loop;
                for (auto it = start; it != path.end(); ++it) loop += *it + " → ";
                loop += to;
                for (auto it = start; it != path.end(); ++it)
                    if (in_cycle.insert(*it).second) issues.push_back({*it, "", "part of a cycle: " + loop});
            } else if (state[to] == 0) {
                visit(to);
            }
        }
        path.pop_back();
        state[id] = 2;
    };
    for (const auto& n : graph.nodes)
        if (state[n.id] == 0) visit(n.id);
    std::unordered_set<std::string> names;
    for (const auto& o : graph.outputs) {
        if (!names.insert(o.name).second) issues.push_back({"", "", std::format("two outputs are called \"{}\"", o.name)});
        const auto t = ix.types.find(o.from.node);
        if (!ix.nodes.contains(o.from.node)) issues.push_back({"", "", std::format("output \"{}\" comes from a node that doesn't exist (\"{}\")", o.name, o.from.node)});
        else if (t != ix.types.end() && !find_pin(t->second->outputs, o.from.pin))
            issues.push_back({"", "", std::format("output \"{}\": \"{}\" has no output \"{}\"", o.name, o.from.node, o.from.pin)});
    }
    return issues;
}

// ---- Evaluation ----

std::uint64_t Evaluation::fingerprint() const {
    Hasher h;
    for (const auto& [name, value] : outputs) {
        h.text(name);
        h.u64(value.hash());
    }
    return h.h;
}

Evaluation evaluate(const Graph& graph, const Registry& registry) {
    Evaluation ev;
    ev.issues = validate(graph, registry);
    const Index ix(graph, registry);
    std::map<std::string, std::string> broken;  // node -> its first issue
    for (const auto& i : ev.issues)
        if (!i.node.empty()) broken.try_emplace(i.node, i.message);

    // Dependency order, ties broken by node id (Kahn's algorithm over a sorted set).
    std::map<std::string, int> waiting;
    std::map<std::string, std::vector<std::string>> next;
    for (const auto& n : graph.nodes) waiting.try_emplace(n.id, 0);
    for (const auto& l : graph.links)
        if (waiting.contains(l.from.node) && waiting.contains(l.to.node)) {
            ++waiting[l.to.node];
            next[l.from.node].push_back(l.to.node);
        }
    std::set<std::string> ready;
    for (const auto& [id, count] : waiting)
        if (count == 0) ready.insert(id);
    while (!ready.empty()) {
        const std::string id = *ready.begin();
        ready.erase(ready.begin());
        ev.order.push_back(id);
        for (const auto& to : next[id])
            if (--waiting[to] == 0) ready.insert(to);
    }

    for (const auto& id : ev.order) {
        NodeResult& res = ev.nodes[id];
        const Node& node = *ix.nodes.at(id);
        if (const auto b = broken.find(id); b != broken.end()) {
            res.state = NodeResult::State::Failed;
            res.message = b->second;
            continue;
        }
        const NodeType& type = *ix.types.at(id);
        NodeContext ctx{node, {}, fill_params(type, node, nullptr), mix(graph.seed ^ hash_text(node.id))};
        // The key: everything the result depends on.
        Hasher key;
        key.text(type.name);
        key.u32(type.version);
        key.text(nlohmann::json(ctx.params).dump());  // sorted keys: the same parameters always hash the same
        key.u64(ctx.seed);
        bool ok = true;
        for (const auto& in : type.inputs) {
            key.text(in.name);
            const auto link = ix.into.find({id, in.name});
            if (link == ix.into.end()) {
                key.u64(0);
                continue;
            }
            const NodeResult& up = ev.nodes[link->second->from.node];
            if (up.state != NodeResult::State::Done || !up.outputs.contains(link->second->from.pin)) {
                res.state = NodeResult::State::Skipped;
                res.message = std::format("input \"{}\" has nothing: {} didn't run", in.name, link->second->from.node);
                ok = false;
                break;
            }
            const Value& incoming = up.outputs.at(link->second->from.pin);
            // Units only known while running (a constant's or a remap's chosen unit) are checked here.
            if (in.unit != Unit::Any && incoming.unit != in.unit) {
                res.state = NodeResult::State::Failed;
                res.message = std::format("input \"{}\" takes {}, but {} gave {}", in.name, unit_name(in.unit), link->second->from.text(), unit_name(incoming.unit));
                ev.issues.push_back({id, in.name, res.message});
                ok = false;
                break;
            }
            key.u64(up.key);
            key.text(link->second->from.pin);
            ctx.inputs[in.name] = incoming;
        }
        if (!ok) continue;
        // Inputs that must share a unit, now that every unit is known (a plain number goes with anything).
        {
            const Value* first = nullptr;
            std::string first_name;
            for (const auto& name : type.same_units) {
                const Value* v = ctx.input(name);
                if (!v || v->unit == Unit::None) continue;
                if (!first) {
                    first = v;
                    first_name = name;
                } else if (v->unit != first->unit) {
                    res.state = NodeResult::State::Failed;
                    res.message = std::format("\"{}\" is in {} but \"{}\" is in {}: they must match", name, unit_name(v->unit), first_name, unit_name(first->unit));
                    ev.issues.push_back({id, name, res.message});
                    ok = false;
                    break;
                }
            }
        }
        if (!ok) continue;
        res.key = key.h;
        const auto t0 = std::chrono::steady_clock::now();
        auto out = type.run(ctx);
        res.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!out) {
            res.state = NodeResult::State::Failed;
            res.message = out.error().message;
            ev.issues.push_back({id, "", res.message});
            continue;
        }
        // Every declared output, with its declared type and resolved unit.
        for (const auto& spec : type.outputs) {
            auto it = out->find(spec.name);
            if (it == out->end() || it->second.type != spec.type) {
                res.state = NodeResult::State::Failed;
                res.message = std::format("the node didn't produce its output \"{}\" (an engine bug)", spec.name);
                break;
            }
            if (spec.unit == Unit::SameAs) it->second.unit = ctx.input(spec.same_as) ? ctx.input(spec.same_as)->unit : Unit::None;
            else if (spec.unit != Unit::Any) it->second.unit = spec.unit;  // Any: the node chose (constant, remap)
            res.bytes += value_bytes(it->second);
        }
        if (!res.message.empty()) {
            ev.issues.push_back({id, "", res.message});
            continue;
        }
        res.outputs = std::move(*out);
        res.state = NodeResult::State::Done;
    }
    // Nodes that never became ready are on a cycle (validation said so).
    for (const auto& n : graph.nodes)
        if (!ev.nodes.contains(n.id)) {
            ev.nodes[n.id].state = NodeResult::State::Failed;
            ev.nodes[n.id].message = broken.contains(n.id) ? broken[n.id] : "part of a cycle";
        }
    for (const auto& o : graph.outputs) {
        const auto it = ev.nodes.find(o.from.node);
        if (it != ev.nodes.end() && it->second.state == NodeResult::State::Done && it->second.outputs.contains(o.from.pin))
            ev.outputs[o.name] = it->second.outputs.at(o.from.pin);
    }
    return ev;
}

} // namespace noor::procgraph
