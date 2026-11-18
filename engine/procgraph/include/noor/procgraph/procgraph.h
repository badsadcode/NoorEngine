#pragma once
// The procedural graph (Milestone 7, ADR-014): graphs are data (`.noorgraph`), nodes are typed,
// versioned functions in a registry, and evaluation is pure: a node's result depends only on its
// type and version, its parameters, its own seed stream and its inputs. That makes every result
// reproducible and gives it a key (a hash of exactly those things) for caching and invalidation.
//
// Editor and tools only (noor_cli, the editor): games load what graphs produce, never this code
// (the runtime-boundary test checks).
//
// A `.noorgraph` file (JSON, format 1):
//   { "noor_graph": 1, "seed": 7,
//     "nodes":   [{"id": "hills", "type": "noise", "version": 1, "params": {...}, "position": [x, y]}],
//     "links":   [{"from": "hills.height", "to": "flat.a"}],
//     "outputs": [{"name": "terrain", "from": "flat.out"}] }
// Node ids are stable names chosen by whoever adds the node; they also seed the node's randomness,
// so adding, removing or reordering other nodes never changes a node's result.
#include "noor/asset/json.h"
#include "noor/foundation/error.h"
#include "noor/foundation/guid.h"
#include "noor/foundation/math.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace noor::procgraph {

// ---- Types and values ----

enum class PinType : std::uint8_t { Scalar, Vector, Seed, Field, InstanceSet, AssetRef };
// Units on pins: a link between different units is an error, never a silent conversion. A height
// field is a Field in metres; a mask is a Field in ratio (0..1). `Any` (inputs only) accepts every
// unit; an output declared `SameAs` takes the unit of one of the node's inputs.
enum class Unit : std::uint8_t { None, Metres, Ratio, Degrees, Any, SameAs };
[[nodiscard]] std::string_view type_name(PinType type);
[[nodiscard]] std::string_view unit_name(Unit unit);

// Fields come in tiles of 256 x 256 cells (257 samples, neighbours share their edges), like terrains.
inline constexpr std::uint32_t kTileCells = 256;

struct FieldShape {
    std::uint32_t tiles_x = 1, tiles_z = 1;
    float cell = 1.0f;  // metres between samples
    [[nodiscard]] std::uint32_t samples_x() const noexcept { return tiles_x * kTileCells + 1; }
    [[nodiscard]] std::uint32_t samples_z() const noexcept { return tiles_z * kTileCells + 1; }
    [[nodiscard]] std::uint32_t tiles() const noexcept { return tiles_x * tiles_z; }
    friend bool operator==(const FieldShape&, const FieldShape&) = default;
};

// One tile of a field: samples x0..x0+256, z0..z0+256 of the whole grid.
struct TileInfo {
    std::uint32_t tx = 0, tz = 0;
    FieldShape shape;
    [[nodiscard]] std::uint32_t x0() const noexcept { return tx * kTileCells; }
    [[nodiscard]] std::uint32_t z0() const noexcept { return tz * kTileCells; }
    // The tile's rectangle in the field's space (metres, centred like a terrain).
    [[nodiscard]] float min_x() const noexcept { return (static_cast<float>(x0()) - 0.5f * static_cast<float>(shape.samples_x() - 1)) * shape.cell; }
    [[nodiscard]] float min_z() const noexcept { return (static_cast<float>(z0()) - 0.5f * static_cast<float>(shape.samples_z() - 1)) * shape.cell; }
    [[nodiscard]] float size() const noexcept { return static_cast<float>(kTileCells) * shape.cell; }
};

// A grid of samples (row by row, z rows of x samples), `cell` metres apart, centred on the origin
// like a terrain (sample (0, 0) is at -width/2, -depth/2).
struct Field {
    std::uint32_t sx = 0, sz = 0;
    float cell = 1.0f;
    std::vector<float> v;
    [[nodiscard]] float at(std::uint32_t x, std::uint32_t z) const noexcept { return v[static_cast<std::size_t>(z) * sx + x]; }
    // Its tiles, when its size is whole tiles (every field a node makes is).
    [[nodiscard]] std::optional<FieldShape> shape() const noexcept {
        if (sx < 2 || sz < 2 || (sx - 1) % kTileCells != 0 || (sz - 1) % kTileCells != 0) return std::nullopt;
        return FieldShape{(sx - 1) / kTileCells, (sz - 1) / kTileCells, cell};
    }
};

struct Instance {
    Guid asset;
    math::Vec3 position;
    math::Quat rotation;
    float scale = 1.0f;
    std::uint64_t seed = 0;
};

struct Value {
    PinType type = PinType::Scalar;
    Unit unit = Unit::None;
    double scalar = 0.0;
    math::Vec3 vector{};
    std::uint64_t seed = 0;
    std::shared_ptr<const Field> field;
    std::shared_ptr<const std::vector<Instance>> instances;
    Guid asset;
    // A fingerprint of the value's content (bits of every number), the same on every machine.
    [[nodiscard]] std::uint64_t hash() const noexcept;
};

// ---- Randomness: explicit streams, no shared generator ----

// SplitMix64: integer maths only, so the same seed gives the same numbers everywhere.
class Random {
public:
    explicit Random(std::uint64_t seed) : state_(seed) {}
    [[nodiscard]] std::uint64_t next() noexcept;
    [[nodiscard]] double uniform() noexcept;  // [0, 1), from the top 53 bits (exact, no library maths)
private:
    std::uint64_t state_;
};
[[nodiscard]] std::uint64_t mix(std::uint64_t x) noexcept;
[[nodiscard]] std::uint64_t hash_text(std::string_view text) noexcept;  // FNV-1a 64

// ---- The graph file ----

struct PinRef {
    std::string node, pin;
    [[nodiscard]] std::string text() const { return node + "." + pin; }
    friend bool operator==(const PinRef&, const PinRef&) = default;
};

struct Node {
    std::string id;
    std::string type;
    std::uint32_t version = 1;
    asset::Json params = asset::Json::object();
    math::Vec2 position{};
    asset::Json extra = asset::Json::object();  // fields this engine doesn't know: kept as they were
};

struct Link {
    PinRef from, to;
};

struct Output {
    std::string name;
    PinRef from;
};

struct Graph {
    static constexpr int kFormat = 1;
    static constexpr std::string_view kExtension = ".noorgraph";
    std::uint64_t seed = 0;
    std::vector<Node> nodes;
    std::vector<Link> links;
    std::vector<Output> outputs;

    [[nodiscard]] static Result<Graph> from_json(const asset::Json& json);
    [[nodiscard]] asset::Json to_json() const;
    [[nodiscard]] static Result<Graph> load(const std::filesystem::path& file);
    [[nodiscard]] Status save(const std::filesystem::path& file) const;
    [[nodiscard]] const Node* find(std::string_view id) const;
};

// ---- Node types ----

struct PinSpec {
    std::string name;
    PinType type = PinType::Scalar;
    Unit unit = Unit::None;
    bool optional = false;       // inputs: may be left unconnected
    std::string same_as;         // outputs with Unit::SameAs: which input's unit
};

struct ParamSpec {
    enum class Kind : std::uint8_t { Number, Integer, Choice, Text, Flag } kind = Kind::Number;
    std::string name;
    asset::Json fallback;               // the default
    double min = -1e300, max = 1e300;   // numbers and integers
    std::vector<std::string> choices;   // Kind::Choice
    std::string help;
};

// What a node's function gets.
struct NodeContext {
    const Node& node;
    std::map<std::string, Value> inputs;  // connected inputs only
    asset::Json params;                   // the node's parameters with defaults filled in and ranges applied
    std::uint64_t seed = 0;               // the node's own seed stream root
    const std::atomic<bool>* cancel = nullptr;
    std::function<std::optional<std::filesystem::path>(const Guid&)> resolve;  // asset id -> file (nodes that read assets)
    // Long nodes check this now and then and return ErrorCode::Cancelled (nothing of theirs is kept).
    [[nodiscard]] bool cancelled() const noexcept { return cancel && cancel->load(std::memory_order_relaxed); }
    [[nodiscard]] Random stream(std::string_view name) const { return Random(mix(seed ^ hash_text(name))); }
    [[nodiscard]] double number(const char* name) const { return params.at(name).get<double>(); }
    [[nodiscard]] std::int64_t integer(const char* name) const { return params.at(name).get<std::int64_t>(); }
    [[nodiscard]] std::string choice(const char* name) const { return params.at(name).get<std::string>(); }
    [[nodiscard]] const Value* input(const std::string& name) const {
        const auto it = inputs.find(name);
        return it == inputs.end() ? nullptr : &it->second;
    }
};

using Outputs = std::map<std::string, Value>;

struct NodeType {
    std::string name;
    std::uint32_t version = 1;
    std::string help;
    std::vector<PinSpec> inputs, outputs;
    std::vector<ParamSpec> params;
    std::vector<std::string> same_units;  // these inputs must share a unit (when connected)
    std::function<Result<Outputs>(const NodeContext&)> run;  // whole-node nodes

    // ---- Tiles (Milestone 7, part 3): regions that change alone ----
    // The node's field output's shape: from its parameters (generators) or its field inputs' shapes.
    // Without this, a field output has its first field input's shape.
    std::function<Result<FieldShape>(const NodeContext&, const std::vector<FieldShape>& inputs)> shape;
    // Tile-local nodes: one tile of the field output (257 x 257 samples) from the same tile of the
    // inputs (whole fields in the context: read your tile). Such a node is cached and recomputed tile by
    // tile; `run` isn't used. The node has exactly one output, a field.
    std::function<Result<std::vector<float>>(const NodeContext&, const TileInfo&)> run_tile;
    // Optional, for tile-local nodes with an input "in": false means the tile is "in" unchanged (a
    // stamp that doesn't reach it). Untouched tiles keep their input's key, so nothing after them reruns.
    std::function<bool(const NodeContext&, const TileInfo&)> touches;
    // Tiled nodes whose field output is declared Unit::Any: the unit they chose (remap's parameter).
    std::function<Unit(const NodeContext&)> unit;
    // Optional: more that the result depends on besides parameters and inputs (a file's size and time).
    std::function<std::string(const NodeContext&)> extra_key;
};

class Registry {
public:
    // The built-in nodes (Milestone 7, part 1: constant, math, noise, combine, clamp, remap).
    [[nodiscard]] static const Registry& builtin();
    void add(NodeType type);
    [[nodiscard]] const NodeType* find(std::string_view name) const;
    [[nodiscard]] const std::map<std::string, NodeType, std::less<>>& all() const noexcept { return types_; }

private:
    std::map<std::string, NodeType, std::less<>> types_;
};

// ---- Checking ----

struct Issue {
    std::string node;   // empty: the graph as a whole
    std::string pin;    // optional
    std::string message;
};

// Everything wrong with a graph, each on the node (and pin) it concerns. A graph with issues still
// loads, saves and evaluates its healthy parts.
[[nodiscard]] std::vector<Issue> validate(const Graph& graph, const Registry& registry);

// ---- Evaluation ----

struct NodeResult {
    enum class State : std::uint8_t { Done, Failed, Skipped } state = State::Skipped;
    Outputs outputs;
    std::uint64_t key = 0;  // hash(type, version, params, seed stream, input keys): what the result depends on
    std::string message;    // Failed / Skipped: why
    double ms = 0.0;
    std::uint64_t bytes = 0;  // size of the outputs
    bool cached = false;      // came from the cache instead of running (tiled: every tile did)
    // Tiled fields: per-tile keys (row by row) and what happened to the tiles this run.
    std::vector<std::uint64_t> tile_keys;
    std::size_t tiles_run = 0, tiles_cached = 0, tiles_passed = 0;  // passed: an input's tile, untouched
};

struct Evaluation {
    std::vector<Issue> issues;                       // from validation and from running
    std::map<std::string, NodeResult> nodes;         // by node id
    std::vector<std::string> order;                  // the order nodes ran in
    std::map<std::string, Value> outputs;            // the graph's declared outputs that could be made
    std::size_t ran = 0, cached = 0;                 // nodes that ran, nodes served from the cache
    std::size_t tiles_run = 0, tiles_cached = 0, tiles_passed = 0;
    bool cancelled = false;
    [[nodiscard]] std::uint64_t fingerprint() const; // of the outputs (names + value hashes)
};

// ---- Cache (Milestone 7, part 2) ----

// Node results by key. In memory, and (with a folder) in files `<key>.npgc` so they outlive the
// process. A key covers everything a result depends on, so a hit is always right; a damaged file is
// a miss (and is replaced). Thread-safe. There is no size limit yet: delete the folder to clear it.
class Cache {
public:
    Cache() = default;                            // memory only
    explicit Cache(std::filesystem::path folder); // memory and files
    [[nodiscard]] std::optional<Outputs> get(std::uint64_t key);
    void put(std::uint64_t key, const Outputs& outputs);
    void forget_memory();                         // keep the files (tests: a "new process")
    [[nodiscard]] const std::filesystem::path& folder() const noexcept { return folder_; }
    [[nodiscard]] std::size_t file_hits() const noexcept { return file_hits_; }

private:
    std::mutex mutex_;
    std::map<std::uint64_t, Outputs> memory_;
    std::filesystem::path folder_;
    std::size_t file_hits_ = 0;
};

// Values to and from bytes (cache files, generated files): every bit kept.
[[nodiscard]] std::string encode(const Value& value);
[[nodiscard]] Result<Value> decode(std::string_view bytes);

// ---- Running ----

struct Progress {
    std::size_t done = 0, total = 0;
    std::string node;   // the node that just finished
    bool cached = false;
};

struct EvaluateOptions {
    Cache* cache = nullptr;
    unsigned threads = 0;  // 0: one per core (at most one per node); 1: on the calling thread
    std::function<void(const Progress&)> progress;  // called after each node, one call at a time
    const std::atomic<bool>* cancel = nullptr;       // set it to stop: running nodes finish or stop, the rest are skipped
    std::function<std::optional<std::filesystem::path>(const Guid&)> resolve;  // asset id -> file, for nodes that read assets
};

// Runs every node that can run. Nodes run on worker threads as soon as their inputs are ready, but
// results never depend on threads: nodes are pure, and `order` is the fixed dependency order (ties by id).
[[nodiscard]] Evaluation evaluate(const Graph& graph, const Registry& registry, const EvaluateOptions& options);
[[nodiscard]] inline Evaluation evaluate(const Graph& graph, const Registry& registry = Registry::builtin()) {
    return evaluate(graph, registry, EvaluateOptions{});
}

// ---- Generating files (headless: noor_cli generate, the editor) ----

struct GenerateOptions {
    std::filesystem::path out;  // the folder for the outputs
    std::string name;           // the graph's name (its file stem): names the fingerprint file
    EvaluateOptions run;
};

struct GenerateReport {
    Evaluation evaluation;
    std::vector<std::filesystem::path> files;  // written, the fingerprint file last
    std::uint64_t fingerprint = 0;
};

// Evaluates and writes every declared output (a file whose bytes wouldn't change isn't rewritten, so its
// time stays put and whoever watches it isn't disturbed): fields as `<output>.noorfield`, the other values in
// `<name>.fingerprint.json` with the graph's hash, its seed, every node's type, version and key, and
// each output's hash. Same graph and versions: the same bytes, on every run. Nothing is written
// unless every output was made (a cancelled or failing run leaves the folder as it was).
[[nodiscard]] Result<GenerateReport> generate(const Graph& graph, const Registry& registry, const GenerateOptions& options);

// A `.noorfield` file: "NFIELD01", u32 version 1, u32 sx, u32 sz, f32 cell, u32 unit, sx*sz f32
// (little-endian, row by row). Readable by any tool: see docs/procgraph/PROCGRAPH.md.
[[nodiscard]] Status save_field(const std::filesystem::path& file, const Field& field, Unit unit);

// Cooking for games (Milestone 7, part 3): runs a graph file and writes its outputs into `out` (the
// editor and noor_cli both use this; games read the files, never the graph).
[[nodiscard]] Result<GenerateReport> cook(const std::filesystem::path& graph_file, const std::filesystem::path& out, Cache* cache,
                                          std::function<std::optional<std::filesystem::path>(const Guid&)> resolve);
[[nodiscard]] Result<std::pair<Field, Unit>> load_field(const std::filesystem::path& file);

} // namespace noor::procgraph
