// Milestone 7, part 2: values as bytes, the results cache, and generating files (headless).
#include "noor/procgraph/procgraph.h"
#include "noor/asset/instance_set.h"

#include "noor/foundation/file_io.h"
#include "noor/foundation/log.h"

#include <bit>
#include <format>

namespace noor::procgraph {
using asset::Json;
namespace stdfs = std::filesystem;

namespace {

struct Writer {
    std::string out;
    void u8(std::uint8_t v) { out.push_back(static_cast<char>(v)); }
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
    void u64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
    void f32(float v) { u32(std::bit_cast<std::uint32_t>(v)); }
    void f64(double v) { u64(std::bit_cast<std::uint64_t>(v)); }
    void text(std::string_view s) {
        u32(static_cast<std::uint32_t>(s.size()));
        out.append(s);
    }
};

struct Reader {
    std::string_view in;
    std::size_t at = 0;
    bool ok = true;
    bool need(std::size_t n) {
        if (at + n > in.size()) ok = false;
        return ok;
    }
    std::uint8_t u8() { return need(1) ? static_cast<std::uint8_t>(in[at++]) : 0; }
    std::uint32_t u32() {
        if (!need(4)) return 0;
        std::uint32_t v = 0;
        for (std::size_t i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(static_cast<unsigned char>(in[at + i])) << (8 * i);
        at += 4;
        return v;
    }
    std::uint64_t u64() {
        if (!need(8)) return 0;
        std::uint64_t v = 0;
        for (std::size_t i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(static_cast<unsigned char>(in[at + i])) << (8 * i);
        at += 8;
        return v;
    }
    float f32() { return std::bit_cast<float>(u32()); }
    double f64() { return std::bit_cast<double>(u64()); }
    std::string text() {
        const std::uint32_t n = u32();
        if (!need(n)) return {};
        std::string s(in.substr(at, n));
        at += n;
        return s;
    }
};

std::string hex(std::uint64_t v) { return std::format("{:016x}", v); }

} // namespace

// ---- Values as bytes ----

std::string encode(const Value& v) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(v.type));
    w.u8(static_cast<std::uint8_t>(v.unit));
    w.f64(v.scalar);
    w.f32(v.vector.x), w.f32(v.vector.y), w.f32(v.vector.z);
    w.u64(v.seed);
    w.u8(v.field ? 1 : 0);
    if (v.field) {
        w.u32(v.field->sx);
        w.u32(v.field->sz);
        w.f32(v.field->cell);
        w.u64(v.field->v.size());
        for (const float f : v.field->v) w.f32(f);
    }
    w.u8(v.instances ? 1 : 0);
    if (v.instances) {
        w.u64(v.instances->size());
        for (const auto& i : *v.instances) {
            w.text(i.asset.to_string());
            w.f32(i.position.x), w.f32(i.position.y), w.f32(i.position.z);
            w.f32(i.rotation.x), w.f32(i.rotation.y), w.f32(i.rotation.z), w.f32(i.rotation.w);
            w.f32(i.scale);
            w.u64(i.seed);
        }
    }
    w.text(v.asset.to_string());
    return std::move(w.out);
}

Result<Value> decode(std::string_view bytes) {
    Reader r{bytes};
    Value v;
    v.type = static_cast<PinType>(r.u8());
    v.unit = static_cast<Unit>(r.u8());
    v.scalar = r.f64();
    v.vector = {r.f32(), r.f32(), r.f32()};
    v.seed = r.u64();
    if (r.u8() == 1) {
        auto f = std::make_shared<Field>();
        f->sx = r.u32();
        f->sz = r.u32();
        f->cell = r.f32();
        const std::uint64_t n = r.u64();
        if (!r.ok || n != static_cast<std::uint64_t>(f->sx) * f->sz || !r.need(n * 4)) return fail(ErrorCode::ParseError, "a damaged field");
        f->v.resize(n);
        for (auto& x : f->v) x = r.f32();
        v.field = std::move(f);
    }
    if (r.u8() == 1) {
        const std::uint64_t n = r.u64();
        if (!r.ok || n > (bytes.size() / 40)) return fail(ErrorCode::ParseError, "a damaged instance set");
        auto list = std::make_shared<std::vector<Instance>>(n);
        for (auto& i : *list) {
            if (auto g = Guid::parse(r.text())) i.asset = *g;
            i.position = {r.f32(), r.f32(), r.f32()};
            i.rotation = {r.f32(), r.f32(), r.f32(), r.f32()};
            i.scale = r.f32();
            i.seed = r.u64();
        }
        v.instances = std::move(list);
    }
    if (auto g = Guid::parse(r.text())) v.asset = *g;
    if (!r.ok) return fail(ErrorCode::ParseError, "a damaged value (it ends too early)");
    return v;
}

// ---- Cache ----

Cache::Cache(stdfs::path folder) : folder_(std::move(folder)) {}

std::optional<Outputs> Cache::get(std::uint64_t key) {
    {
        std::scoped_lock lock(mutex_);
        if (const auto it = memory_.find(key); it != memory_.end()) return it->second;
    }
    if (folder_.empty()) return std::nullopt;
    const auto file = folder_ / (hex(key) + ".npgc");
    std::error_code ec;
    if (!stdfs::exists(file, ec)) return std::nullopt;
    auto bytes = fs::read_file(file, 1ull << 31);
    if (!bytes) return std::nullopt;
    Reader r{*bytes};
    const bool header = bytes->size() >= 12 && bytes->compare(0, 8, "NPGCACHE") == 0;
    r.at = 8;
    Outputs out;
    bool ok = header && r.u32() == 1 && r.u64() == key;
    const std::uint32_t count = ok ? r.u32() : 0;
    for (std::uint32_t i = 0; ok && i < count; ++i) {
        const std::string name = r.text();
        const std::string blob = r.text();
        auto v = r.ok ? decode(blob) : Result<Value>(fail(ErrorCode::ParseError, "short"));
        if (!v) ok = false;
        else out[name] = std::move(*v);
    }
    if (!ok || !r.ok) {
        NOOR_LOG_WARN("ProcGraph", "Cache file {} is damaged: computing it again", file.filename().string());
        return std::nullopt;
    }
    std::scoped_lock lock(mutex_);
    ++file_hits_;
    memory_[key] = out;
    return out;
}

void Cache::put(std::uint64_t key, const Outputs& outputs) {
    {
        std::scoped_lock lock(mutex_);
        memory_[key] = outputs;
    }
    if (folder_.empty()) return;
    Writer w;
    w.out = "NPGCACHE";
    w.u32(1);
    w.u64(key);
    w.u32(static_cast<std::uint32_t>(outputs.size()));
    for (const auto& [name, v] : outputs) {
        w.text(name);
        w.text(encode(v));
    }
    std::error_code ec;
    stdfs::create_directories(folder_, ec);
    // Atomic: a cancelled or crashed run never leaves half a file behind.
    if (auto s = fs::write_file_atomic(folder_ / (hex(key) + ".npgc"), w.out, false); !s)
        NOOR_LOG_WARN("ProcGraph", "Couldn't write a cache file: {}", s.error().describe());
}

void Cache::forget_memory() {
    std::scoped_lock lock(mutex_);
    memory_.clear();
}

// ---- Field files ----

namespace {

// Writes `bytes` unless the file already holds exactly them.
Status write_if_changed(const stdfs::path& file, const std::string& bytes) {
    std::error_code ec;
    if (stdfs::exists(file, ec) && stdfs::file_size(file, ec) == bytes.size())
        if (auto old = fs::read_file(file, 1ull << 31); old && *old == bytes) return {};
    return fs::write_file_atomic(file, bytes, false);
}

std::string field_bytes(const Field& f, Unit unit);

} // namespace

Status save_field(const stdfs::path& file, const Field& f, Unit unit) { return fs::write_file_atomic(file, field_bytes(f, unit), false); }

namespace {
std::string field_bytes(const Field& f, Unit unit) {
    Writer w;
    w.out = "NFIELD01";
    w.u32(1);
    w.u32(f.sx);
    w.u32(f.sz);
    w.f32(f.cell);
    w.u32(static_cast<std::uint32_t>(unit));
    w.out.reserve(w.out.size() + f.v.size() * 4);
    for (const float x : f.v) w.f32(x);
    return std::move(w.out);
}
} // namespace

Result<std::pair<Field, Unit>> load_field(const stdfs::path& file) {
    auto bytes = fs::read_file(file, 1ull << 31);
    if (!bytes) return std::unexpected(bytes.error());
    if (bytes->size() < 28 || bytes->compare(0, 8, "NFIELD01") != 0) return fail(ErrorCode::ParseError, std::format("{} is not a .noorfield file", file.filename().string()));
    Reader r{*bytes};
    r.at = 8;
    if (r.u32() != 1) return fail(ErrorCode::ParseError, "a .noorfield version this engine doesn't read");
    Field f;
    f.sx = r.u32();
    f.sz = r.u32();
    f.cell = r.f32();
    const auto unit = static_cast<Unit>(r.u32());
    const std::size_t n = static_cast<std::size_t>(f.sx) * f.sz;
    if (bytes->size() != 28 + n * 4) return fail(ErrorCode::ParseError, std::format("{} is damaged (its size doesn't match its header)", file.filename().string()));
    f.v.resize(n);
    for (auto& x : f.v) x = r.f32();
    return std::pair{std::move(f), unit};
}

// ---- Generating ----

asset::InstanceSet to_instance_set(const std::vector<Instance>& list) {
    asset::InstanceSet set;
    std::map<Guid, std::uint32_t> index;  // assets in order of first use
    for (const auto& i : list) {
        auto [it, fresh] = index.try_emplace(i.asset, static_cast<std::uint32_t>(set.assets.size()));
        if (fresh) set.assets.push_back(i.asset);
        set.items.push_back({it->second, i.seed, i.position, i.rotation, i.scale});
    }
    return set;
}

Result<GenerateReport> generate(const Graph& graph, const Registry& registry, const GenerateOptions& options) {
    GenerateReport report;
    report.evaluation = evaluate(graph, registry, options.run);
    const Evaluation& ev = report.evaluation;
    if (ev.cancelled) return fail(ErrorCode::Cancelled, "cancelled: nothing was written");
    std::vector<std::string> missing;
    for (const auto& o : graph.outputs)
        if (!ev.outputs.contains(o.name)) missing.push_back(o.name);
    if (graph.outputs.empty()) return fail(ErrorCode::InvalidArgument, "the graph declares no outputs: nothing to write");
    if (!missing.empty()) {
        std::string list;
        for (const auto& m : missing) list += (list.empty() ? "" : ", ") + m;
        return fail(ErrorCode::InvalidArgument, std::format("output(s) {} couldn't be made (see the issues): nothing was written", list));
    }
    report.fingerprint = ev.fingerprint();

    // The fingerprint file: what made these outputs, enough to tell whether they're current.
    Json fp;
    fp["noor_graph_fingerprint"] = 1;
    fp["graph"] = options.name;
    fp["graph_hash"] = hex(hash_text(asset::to_text(graph.to_json())));
    fp["seed"] = graph.seed;
    Json nodes = Json::object();
    std::vector<std::string> ids;
    for (const auto& n : graph.nodes) ids.push_back(n.id);
    std::ranges::sort(ids);
    for (const auto& id : ids) {
        const Node* n = graph.find(id);
        const auto& r = ev.nodes.at(id);
        nodes[id] = {{"type", n->type}, {"version", n->version}, {"key", hex(r.key)}};
    }
    fp["nodes"] = std::move(nodes);
    Json outputs = Json::object();
    std::error_code ec;
    stdfs::create_directories(options.out, ec);
    if (ec) return fail(ErrorCode::IoError, std::format("can't create {}: {}", options.out.generic_string(), ec.message()));
    for (const auto& [name, v] : ev.outputs) {
        Json o{{"type", std::string(type_name(v.type))}, {"unit", std::string(unit_name(v.unit))}, {"hash", hex(v.hash())}};
        if (v.type == PinType::Field && v.field) {
            const auto file = options.out / (name + ".noorfield");
            if (auto s = write_if_changed(file, field_bytes(*v.field, v.unit)); !s) return std::unexpected(s.error());
            report.files.push_back(file);
            o["file"] = file.filename().generic_string();
        } else if (v.type == PinType::InstanceSet && v.instances) {
            // An instance set: a plain JSON file any tool can read (noor/asset/instance_set.h).
            const auto file = options.out / (name + ".instances.json");
            if (auto s = write_if_changed(file, to_instance_set(*v.instances).to_text()); !s) return std::unexpected(s.error());
            report.files.push_back(file);
            o["file"] = file.filename().generic_string();
            o["count"] = v.instances->size();
        } else if (v.type == PinType::Scalar) {
            o["value"] = v.scalar;
        } else if (v.type == PinType::Seed) {
            o["value"] = v.seed;
        } else if (v.type == PinType::Vector) {
            o["value"] = {v.vector.x, v.vector.y, v.vector.z};
        } else if (v.type == PinType::AssetRef) {
            o["value"] = v.asset.to_string();
        }
        outputs[name] = std::move(o);
    }
    fp["outputs"] = std::move(outputs);
    fp["fingerprint"] = hex(report.fingerprint);
    const auto fp_file = options.out / ((options.name.empty() ? std::string("graph") : options.name) + ".fingerprint.json");
    if (auto s = write_if_changed(fp_file, asset::to_text(fp)); !s) return std::unexpected(s.error());
    report.files.push_back(fp_file);
    return report;
}

Result<GenerateReport> cook(const stdfs::path& graph_file, const stdfs::path& out, Cache* cache,
                            std::function<std::optional<stdfs::path>(const Guid&)> resolve) {
    auto graph = Graph::load(graph_file);
    if (!graph) return std::unexpected(graph.error());
    GenerateOptions opts;
    opts.out = out;
    opts.name = "graph";
    opts.run.cache = cache;
    opts.run.resolve = std::move(resolve);
    return generate(*graph, Registry::builtin(), opts);
}

} // namespace noor::procgraph
