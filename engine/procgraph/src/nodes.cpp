// The built-in nodes (Milestone 7, parts 1 and 3). Every node is pure: its outputs depend only on its
// parameters, its seed stream and its inputs. Bump a node's version whenever its results change, or
// cached results would be served stale. Field nodes work tile by tile where they can (run_tile), so a
// change in one region recomputes only that region.
#include "noor/procgraph/procgraph.h"

#include "noor/terrain/terrain.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace noor::procgraph {
using asset::Json;

namespace {

using Kind = ParamSpec::Kind;
constexpr std::uint32_t kTileSamples = kTileCells + 1;
constexpr std::size_t kTileSize = static_cast<std::size_t>(kTileSamples) * kTileSamples;

ParamSpec number(std::string name, double fallback, double min, double max, std::string help) {
    return {Kind::Number, std::move(name), fallback, min, max, {}, std::move(help)};
}
ParamSpec integer(std::string name, std::int64_t fallback, double min, double max, std::string help) {
    return {Kind::Integer, std::move(name), fallback, min, max, {}, std::move(help)};
}
ParamSpec choice(std::string name, std::string fallback, std::vector<std::string> choices, std::string help) {
    return {Kind::Choice, std::move(name), fallback, 0, 0, std::move(choices), std::move(help)};
}
ParamSpec text(std::string name, std::string help) { return {Kind::Text, std::move(name), std::string(), 0, 0, {}, std::move(help)}; }

Unit unit_from(const std::string& s) { return s == "metres" ? Unit::Metres : s == "ratio" ? Unit::Ratio : s == "degrees" ? Unit::Degrees : Unit::None; }

Value scalar(double v) {
    Value out;
    out.type = PinType::Scalar;
    out.scalar = v;
    return out;
}

Value field_value(std::shared_ptr<Field> f) {
    Value out;
    out.type = PinType::Field;
    out.field = std::move(f);
    return out;
}

double apply(const std::string& op, double a, double b) {
    if (op == "add") return a + b;
    if (op == "subtract") return a - b;
    if (op == "multiply") return a * b;
    if (op == "divide") return b == 0.0 ? 0.0 : a / b;  // a defined result, never NaN or infinity
    if (op == "min") return std::min(a, b);
    return std::max(a, b);
}

const std::vector<std::string> kOps = {"add", "subtract", "multiply", "divide", "min", "max"};

double smoothstep(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// cos for 0..90 degrees from + - * only (Taylor series to x^14: error below 1e-8 on [0, pi/2]), so slope
// thresholds are the same on every machine.
double cos_degrees(double deg) {
    const double x = std::clamp(deg, 0.0, 90.0) * (3.14159265358979323846 / 180.0), x2 = x * x;
    double term = 1.0, sum = 1.0;
    for (int n = 1; n <= 7; ++n) {
        term *= -x2 / static_cast<double>((2 * n - 1) * (2 * n));
        sum += term;
    }
    return sum;
}

// Visits a tile's samples: f(global index, local index).
template <class F>
void each_sample(const TileInfo& t, F&& f) {
    const std::uint32_t sx = t.shape.samples_x();
    for (std::uint32_t z = 0; z < kTileSamples; ++z)
        for (std::uint32_t x = 0; x < kTileSamples; ++x)
            f(static_cast<std::size_t>(t.z0() + z) * sx + t.x0() + x, static_cast<std::size_t>(z) * kTileSamples + x);
}

// A tile from one input field and a sample function.
template <class F>
Result<std::vector<float>> map_tile(const NodeContext& c, const TileInfo& t, F&& fn) {
    const Field& in = *c.input("in")->field;
    std::vector<float> out(kTileSize);
    each_sample(t, [&](std::size_t g, std::size_t l) { out[l] = fn(in.v[g]); });
    return out;
}

Result<FieldShape> shape_from_params(const NodeContext& c, const std::vector<FieldShape>&) {
    return FieldShape{static_cast<std::uint32_t>(c.integer("tiles_x")), static_cast<std::uint32_t>(c.integer("tiles_z")), static_cast<float>(c.number("cell_size"))};
}

std::vector<ParamSpec> size_params() {
    return {integer("tiles_x", 1, 1, 16, "Tiles along x (256 cells each)"), integer("tiles_z", 1, 1, 16, "Tiles along z"),
            number("cell_size", 1.0, 0.05, 64.0, "Metres between samples")};
}

} // namespace

const Registry& Registry::builtin() {
    static const Registry registry = [] {
        Registry r;

        r.add({.name = "constant", .version = 1, .help = "A number, optionally with a unit.",
               .outputs = {{"value", PinType::Scalar, Unit::Any, false, ""}},  // the unit parameter decides
               .params = {number("value", 0.0, -1e12, 1e12, "The number"), choice("unit", "none", {"none", "metres", "ratio", "degrees"}, "Its unit")},
               .run = [](const NodeContext& c) -> Result<Outputs> {
                   Value v = scalar(c.number("value"));
                   v.unit = unit_from(c.choice("unit"));
                   return Outputs{{"value", v}};
               }});

        r.add({.name = "math", .version = 1, .help = "Two numbers combined: add, subtract, multiply, divide (by zero gives 0), min or max.",
               .inputs = {{"a", PinType::Scalar, Unit::Any, false, ""}, {"b", PinType::Scalar, Unit::Any, false, ""}},
               .outputs = {{"out", PinType::Scalar, Unit::SameAs, false, "a"}},
               .params = {choice("op", "add", kOps, "What to do")},
               .same_units = {"a", "b"},
               .run = [](const NodeContext& c) -> Result<Outputs> {
                   return Outputs{{"out", scalar(apply(c.choice("op"), c.input("a")->scalar, c.input("b")->scalar))}};
               }});

        auto noise_params = size_params();
        noise_params.push_back(number("height", 40.0, 0.0, 10000.0, "Highest point (m)"));
        noise_params.push_back(number("scale", 250.0, 1.0, 100000.0, "Size of the largest hills (m)"));
        noise_params.push_back(integer("octaves", 5, 1, 12, "Detail layers"));
        r.add({.name = "noise", .version = 1,
               .help = "Rolling hills: the terrain's deterministic fractal noise (integer hashing, + - * / only), "
                       "heights from 0 to `height` metres, features about `scale` metres across.",
               .outputs = {{"height", PinType::Field, Unit::Metres, false, ""}},
               .params = noise_params,
               .shape = shape_from_params,
               .run_tile = [](const NodeContext& c, const TileInfo& t) -> Result<std::vector<float>> {
                   const double scale = c.number("scale"), cell = t.shape.cell;
                   const float height = static_cast<float>(c.number("height"));
                   const auto octaves = static_cast<std::uint32_t>(c.integer("octaves"));
                   const std::uint64_t seed = c.stream("noise").next();
                   std::vector<float> out(kTileSize);
                   for (std::uint32_t z = 0; z < kTileSamples; ++z) {
                       if (c.cancelled()) return fail(ErrorCode::Cancelled, "cancelled");  // a row at a time
                       for (std::uint32_t x = 0; x < kTileSamples; ++x) {
                           // The same sums as terrain::Heights::build, so a terrain with this seed matches bit for bit.
                           const double px = static_cast<double>(t.x0() + x) * cell / scale, pz = static_cast<double>(t.z0() + z) * cell / scale;
                           out[static_cast<std::size_t>(z) * kTileSamples + x] = (terrain::fractal_noise(px, pz, seed, octaves) * 0.5f + 0.5f) * height;
                       }
                   }
                   return out;
               }});

        r.add({.name = "combine", .version = 1,
               .help = "Two fields sample by sample: add, subtract, multiply, divide, min, max, or lerp (a to b by mask t). "
                       "Both must be the same size and unit.",
               .inputs = {{"a", PinType::Field, Unit::Any, false, ""}, {"b", PinType::Field, Unit::Any, false, ""}, {"t", PinType::Field, Unit::Ratio, true, ""}},
               .outputs = {{"out", PinType::Field, Unit::SameAs, false, "a"}},
               .params = {choice("op", "add", {"add", "subtract", "multiply", "divide", "min", "max", "lerp"}, "What to do")},
               .same_units = {"a", "b"},
               .run_tile = [](const NodeContext& c, const TileInfo& t) -> Result<std::vector<float>> {
                   const Field& a = *c.input("a")->field;
                   const Field& b = *c.input("b")->field;
                   const Field* m = c.input("t") ? c.input("t")->field.get() : nullptr;
                   const std::string op = c.choice("op");
                   if (op == "lerp" && !m) return fail(ErrorCode::InvalidArgument, "lerp needs the mask input \"t\"");
                   std::vector<float> out(kTileSize);
                   each_sample(t, [&](std::size_t g, std::size_t l) {
                       out[l] = op == "lerp" ? a.v[g] + (b.v[g] - a.v[g]) * std::clamp(m->v[g], 0.0f, 1.0f) : static_cast<float>(apply(op, a.v[g], b.v[g]));
                   });
                   return out;
               }});

        r.add({.name = "clamp", .version = 1, .help = "Keeps every sample between min and max.",
               .inputs = {{"in", PinType::Field, Unit::Any, false, ""}},
               .outputs = {{"out", PinType::Field, Unit::SameAs, false, "in"}},
               .params = {number("min", 0.0, -1e12, 1e12, "Lowest"), number("max", 1.0, -1e12, 1e12, "Highest")},
               .run_tile = [](const NodeContext& c, const TileInfo& t) -> Result<std::vector<float>> {
                   const float lo = static_cast<float>(c.number("min")), hi = static_cast<float>(c.number("max"));
                   if (lo > hi) return fail(ErrorCode::InvalidArgument, std::format("min ({}) is above max ({})", lo, hi));
                   return map_tile(c, t, [&](float v) { return std::clamp(v, lo, hi); });
               }});

        r.add({.name = "remap", .version = 1,
               .help = "Maps from_min..from_max onto to_min..to_max (straight line, clamped at the ends). Its output unit: "
                       "\"ratio\" turns heights into a mask, \"metres\" a mask into heights, \"same\" keeps the input's.",
               .inputs = {{"in", PinType::Field, Unit::Any, false, ""}},
               .outputs = {{"out", PinType::Field, Unit::Any, false, ""}},
               .params = {number("from_min", 0.0, -1e12, 1e12, "Input low"), number("from_max", 1.0, -1e12, 1e12, "Input high"),
                          number("to_min", 0.0, -1e12, 1e12, "Output low"), number("to_max", 1.0, -1e12, 1e12, "Output high"),
                          choice("unit", "same", {"same", "metres", "ratio", "none"}, "The output's unit")},
               .run_tile = [](const NodeContext& c, const TileInfo& t) -> Result<std::vector<float>> {
                   const double a = c.number("from_min"), b = c.number("from_max"), lo = c.number("to_min"), hi = c.number("to_max");
                   if (a == b) return fail(ErrorCode::InvalidArgument, "from_min and from_max are the same: nothing to map");
                   return map_tile(c, t, [&](float v) { return static_cast<float>(lo + (hi - lo) * std::clamp((static_cast<double>(v) - a) / (b - a), 0.0, 1.0)); });
               },
               .unit = [](const NodeContext& c) {
                   const std::string u = c.choice("unit");
                   return u == "same" ? (c.input("in") ? c.input("in")->unit : Unit::None) : unit_from(u);
               }});

        // ---- Terrain nodes (part 3) ----

        r.add({.name = "stamp", .version = 1,
               .help = "A hill (or, with a negative height, a hollow) at a point: `hill` is round-topped, `plateau` flat-topped. "
                       "Tiles it doesn't reach are passed through untouched, so moving a stamp recomputes only the tiles it covers.",
               .inputs = {{"in", PinType::Field, Unit::Metres, false, ""}},
               .outputs = {{"out", PinType::Field, Unit::Metres, false, ""}},
               .params = {number("x", 0.0, -1e6, 1e6, "Centre x (m, in the terrain's space)"), number("z", 0.0, -1e6, 1e6, "Centre z (m)"),
                          number("radius", 20.0, 0.1, 1e5, "Radius (m)"), number("height", 10.0, -1e4, 1e4, "Height at the centre (m; negative: a hollow)"),
                          choice("shape", "hill", {"hill", "plateau"}, "Round or flat top")},
               .run_tile = [](const NodeContext& c, const TileInfo& t) -> Result<std::vector<float>> {
                   const double cx = c.number("x"), cz = c.number("z"), r = c.number("radius"), h = c.number("height");
                   const bool plateau = c.choice("shape") == "plateau";
                   const Field& in = *c.input("in")->field;
                   const double cell = t.shape.cell, ox = -0.5 * cell * (t.shape.samples_x() - 1), oz = -0.5 * cell * (t.shape.samples_z() - 1);
                   std::vector<float> out(kTileSize);
                   for (std::uint32_t z = 0; z < kTileSamples; ++z)
                       for (std::uint32_t x = 0; x < kTileSamples; ++x) {
                           const std::size_t g = static_cast<std::size_t>(t.z0() + z) * t.shape.samples_x() + t.x0() + x;
                           const double dx = ox + (t.x0() + x) * cell - cx, dz = oz + (t.z0() + z) * cell - cz;
                           const double q = (dx * dx + dz * dz) / (r * r);  // (distance / radius)^2: no square root needed
                           double w = 0.0;
                           if (q < 1.0) w = plateau ? (q < 0.36 ? 1.0 : 1.0 - smoothstep((q - 0.36) / 0.64)) : (1.0 - q) * (1.0 - q);
                           out[static_cast<std::size_t>(z) * kTileSamples + x] = static_cast<float>(in.v[g] + h * w);
                       }
                   return out;
               },
               .touches = [](const NodeContext& c, const TileInfo& t) {
                   // Does the circle reach the tile's rectangle? (The closest point of the rectangle to the centre.)
                   const double cx = c.number("x"), cz = c.number("z"), r = c.number("radius");
                   const double px = std::clamp(cx, static_cast<double>(t.min_x()), static_cast<double>(t.min_x() + t.size()));
                   const double pz = std::clamp(cz, static_cast<double>(t.min_z()), static_cast<double>(t.min_z() + t.size()));
                   return (px - cx) * (px - cx) + (pz - cz) * (pz - cz) < r * r;
               }});

        r.add({.name = "terrace", .version = 1,
               .help = "Steps: heights snap to multiples of `step`, with a ramp between levels (`sharpness` 1: cliffs, 0: no steps).",
               .inputs = {{"in", PinType::Field, Unit::Metres, false, ""}},
               .outputs = {{"out", PinType::Field, Unit::Metres, false, ""}},
               .params = {number("step", 5.0, 0.01, 1e4, "Height of a step (m)"), number("sharpness", 0.7, 0.0, 1.0, "How flat the levels are")},
               .run_tile = [](const NodeContext& c, const TileInfo& t) -> Result<std::vector<float>> {
                   const double step = c.number("step"), ramp = 1.0 - c.number("sharpness");  // the part of a step that slopes
                   return map_tile(c, t, [&](float v) {
                       const double k = v / step, level = std::floor(k), f = k - level;
                       const double rise = ramp <= 0.0 ? 0.0 : smoothstep((f - (1.0 - ramp)) / ramp);
                       return static_cast<float>((level + (ramp >= 1.0 ? f : rise)) * step);
                   });
               }});

        r.add({.name = "height_band", .version = 1,
               .help = "A mask: 1 between min and max height, fading to 0 over `soft` metres at each edge (snow lines, valley floors).",
               .inputs = {{"in", PinType::Field, Unit::Metres, false, ""}},
               .outputs = {{"out", PinType::Field, Unit::Ratio, false, ""}},
               .params = {number("min", 0.0, -1e5, 1e5, "Lowest height in the band (m)"), number("max", 10.0, -1e5, 1e5, "Highest (m)"),
                          number("soft", 1.0, 0.0, 1e4, "Fade width at each edge (m)")},
               .run_tile = [](const NodeContext& c, const TileInfo& t) -> Result<std::vector<float>> {
                   const double lo = c.number("min"), hi = c.number("max"), s = c.number("soft");
                   if (lo > hi) return fail(ErrorCode::InvalidArgument, std::format("min ({}) is above max ({})", lo, hi));
                   const auto edge = [s](double v, double at) { return s <= 0.0 ? (v >= at ? 1.0 : 0.0) : smoothstep((v - (at - 0.5 * s)) / s); };
                   return map_tile(c, t, [&](float v) { return static_cast<float>(edge(v, lo) * (1.0 - edge(v, hi))); });
               }});

        r.add({.name = "slope", .version = 1,
               .help = "A mask from steepness: 0 flatter than min_degrees, 1 steeper than max_degrees, smooth between (rock on cliffs). "
                       "Reads neighbouring samples, so it works on the whole field (any change recomputes all of it).",
               .inputs = {{"in", PinType::Field, Unit::Metres, false, ""}},
               .outputs = {{"out", PinType::Field, Unit::Ratio, false, ""}},
               .params = {number("min_degrees", 25.0, 0.0, 90.0, "Where the mask starts"), number("max_degrees", 40.0, 0.0, 90.0, "Where it's full")},
               .run = [](const NodeContext& c) -> Result<Outputs> {
                   const double a = c.number("min_degrees"), b = c.number("max_degrees");
                   if (a >= b) return fail(ErrorCode::InvalidArgument, "min_degrees must be below max_degrees");
                   const double ca = cos_degrees(a), cb = cos_degrees(b);  // cos falls as the slope rises
                   const Field& in = *c.input("in")->field;
                   auto f = std::make_shared<Field>(in);
                   for (std::uint32_t z = 0; z < in.sz; ++z) {
                       if (c.cancelled()) return fail(ErrorCode::Cancelled, "cancelled");
                       for (std::uint32_t x = 0; x < in.sx; ++x) {
                           const std::uint32_t xl = x > 0 ? x - 1 : x, xr = x + 1 < in.sx ? x + 1 : x, zl = z > 0 ? z - 1 : z, zr = z + 1 < in.sz ? z + 1 : z;
                           const double gx = (in.at(xr, z) - in.at(xl, z)) / ((xr - xl) * in.cell), gz = (in.at(x, zr) - in.at(x, zl)) / ((zr - zl) * in.cell);
                           const double cos_slope = 1.0 / std::sqrt(1.0 + gx * gx + gz * gz);  // the normal's y
                           f->v[static_cast<std::size_t>(z) * in.sx + x] = static_cast<float>(smoothstep((ca - cos_slope) / (ca - cb)));
                       }
                   }
                   return Outputs{{"out", field_value(std::move(f))}};
               }});

        auto heightmap_params = size_params();
        heightmap_params.push_back(text("image", "The picture (an asset id): black is min_height, white max_height; 16-bit PNGs keep the most steps"));
        heightmap_params.push_back(number("min_height", 0.0, -1e5, 1e5, "Height of black (m)"));
        heightmap_params.push_back(number("max_height", 50.0, -1e5, 1e5, "Height of white (m)"));
        r.add({.name = "heightmap", .version = 1,
               .help = "Heights from a greyscale picture (an asset), stretched over the whole field (bilinear), like a terrain's heightmap base.",
               .outputs = {{"height", PinType::Field, Unit::Metres, false, ""}},
               .params = heightmap_params,
               .run = [](const NodeContext& c) -> Result<Outputs> {
                   const auto id = Guid::parse(c.choice("image"));
                   if (!id) return fail(ErrorCode::InvalidArgument, "\"image\" isn't an asset id");
                   const auto file = c.resolve ? c.resolve(*id) : std::nullopt;
                   if (!file) return fail(ErrorCode::NotFound, std::format("the picture {} isn't in the project", id->to_string()));
                   terrain::Settings s;
                   s.tiles_x = static_cast<std::uint32_t>(c.integer("tiles_x"));
                   s.tiles_z = static_cast<std::uint32_t>(c.integer("tiles_z"));
                   s.cell_size = static_cast<float>(c.number("cell_size"));
                   s.base = terrain::Settings::Base::Heightmap;
                   s.heightmap = *file;
                   s.min_height = static_cast<float>(c.number("min_height"));
                   s.max_height = static_cast<float>(c.number("max_height"));
                   auto h = terrain::Heights::build(s);
                   if (!h) return std::unexpected(h.error());
                   auto f = std::make_shared<Field>();
                   f->sx = h->samples_x();
                   f->sz = h->samples_z();
                   f->cell = h->cell_size();
                   f->v.resize(static_cast<std::size_t>(f->sx) * f->sz);
                   for (std::uint32_t z = 0; z < f->sz; ++z)
                       for (std::uint32_t x = 0; x < f->sx; ++x) f->v[static_cast<std::size_t>(z) * f->sx + x] = h->at(x, z);
                   return Outputs{{"height", field_value(std::move(f))}};
               },
               .shape = shape_from_params,
               .extra_key = [](const NodeContext& c) -> std::string {
                   // The picture's size and time: a new picture under the same id is a new result.
                   const auto id = Guid::parse(c.choice("image"));
                   const auto file = id && c.resolve ? c.resolve(*id) : std::nullopt;
                   std::error_code ec;
                   if (!file || !std::filesystem::exists(*file, ec)) return "missing";
                   return std::format("{} {}", std::filesystem::file_size(*file, ec), std::filesystem::last_write_time(*file, ec).time_since_epoch().count());
               }});
        return r;
    }();
    return registry;
}

} // namespace noor::procgraph
