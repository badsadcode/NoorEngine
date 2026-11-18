// The built-in nodes (Milestone 7, parts 1 and 3). Every node is pure: its outputs depend only on its
// parameters, its seed stream and its inputs. Bump a node's version whenever its results change, or
// cached results would be served stale. Field nodes work tile by tile where they can (run_tile), so a
// change in one region recomputes only that region.
#include "noor/procgraph/procgraph.h"

#include "noor/terrain/terrain.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <unordered_map>

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

// sin and cos of 0 <= x < 2 pi from + - * only (Taylor series to x^17 on [0, pi/2] after folding), so
// scattered rotations are the same on every machine.
std::pair<double, double> sin_cos(double x) {
    constexpr double kPi = 3.14159265358979323846;
    x = std::clamp(x, 0.0, 2.0 * kPi);
    double s_sign = 1.0, c_sign = 1.0;
    if (x > kPi) {  // sin(x) = -sin(x - pi), cos(x) = -cos(x - pi)
        x -= kPi;
        s_sign = -1.0;
        c_sign = -1.0;
    }
    if (x > 0.5 * kPi) {  // sin(x) = sin(pi - x), cos(x) = -cos(pi - x)
        x = kPi - x;
        c_sign = -c_sign;
    }
    const double x2 = x * x;
    double st = x, s = x, ct = 1.0, c = 1.0;
    for (int n = 1; n <= 8; ++n) {
        st *= -x2 / static_cast<double>((2 * n) * (2 * n + 1));
        ct *= -x2 / static_cast<double>((2 * n - 1) * (2 * n));
        s += st;
        c += ct;
    }
    return {s_sign * s, c_sign * c};
}

// Bilinear sample of a field at (x, z) metres in its centred space; outside: the nearest edge.
double sample(const Field& f, double x, double z) {
    const double fx = std::clamp(x / f.cell + 0.5 * (f.sx - 1), 0.0, static_cast<double>(f.sx - 1));
    const double fz = std::clamp(z / f.cell + 0.5 * (f.sz - 1), 0.0, static_cast<double>(f.sz - 1));
    const auto x0 = static_cast<std::uint32_t>(fx), z0 = static_cast<std::uint32_t>(fz);
    const std::uint32_t x1 = std::min(x0 + 1, f.sx - 1), z1 = std::min(z0 + 1, f.sz - 1);
    const double tx = fx - x0, tz = fz - z0;
    const double a = f.at(x0, z0) + (f.at(x1, z0) - f.at(x0, z0)) * tx;
    const double b = f.at(x0, z1) + (f.at(x1, z1) - f.at(x0, z1)) * tx;
    return a + (b - a) * tz;
}

// "x,z x,z ...; x,z ..." -> polylines (exclude's paths); "x,z,r; ..." -> circles. Bad text: an error.
Result<std::vector<std::vector<double>>> parse_groups(const std::string& text, std::size_t per_point, const char* what) {
    std::vector<std::vector<double>> groups;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find(';', start), text.size());
        std::vector<double> numbers;
        std::string part = text.substr(start, end - start);
        for (char& ch : part)
            if (ch == ',') ch = ' ';
        const char* p = part.c_str();
        const char* e = p + part.size();
        while (p < e) {
            while (p < e && (*p == ' ' || *p == '\t' || *p == '\n')) ++p;
            if (p >= e) break;
            double v = 0;
            const auto [next, ec] = std::from_chars(p, e, v);
            if (ec != std::errc{}) return fail(ErrorCode::InvalidArgument, std::format("{}: \"{}\" isn't a list of numbers", what, part));
            numbers.push_back(v);
            p = next;
        }
        if (!numbers.empty()) {
            if (numbers.size() % per_point != 0)
                return fail(ErrorCode::InvalidArgument, std::format("{}: each point needs {} numbers (\"{}\")", what, per_point, part));
            groups.push_back(std::move(numbers));
        }
        start = end + 1;
    }
    return groups;
}

Value instances_value(std::vector<Instance> list) {
    Value out;
    out.type = PinType::InstanceSet;
    out.instances = std::make_shared<const std::vector<Instance>>(std::move(list));
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
        // ---- Masks from hand-made layers (part 8) ----

        auto paint_params = size_params();
        paint_params.push_back(text("paint", "A terrain's paint layer (the .noorpaint asset's id)"));
        paint_params.push_back(integer("layer", 0, 0, 3, "Which material layer: 0 is the ground underneath, 1-3 the painted ones"));
        r.add({.name = "paint_mask", .version = 1,
               .help = "A mask from a terrain's paint: how much of one material layer each sample has (0..1). Plant where the meadow "
                       "was painted, and nowhere on the path or the rock. The field matches the terrain when its tiles and cell size do.",
               .outputs = {{"mask", PinType::Field, Unit::Ratio, false, ""}},
               .params = paint_params,
               .run = [](const NodeContext& c) -> Result<Outputs> {
                   const auto id = Guid::parse(c.choice("paint"));
                   if (!id) return fail(ErrorCode::InvalidArgument, "\"paint\" isn't an asset id: pick the terrain's paint layer");
                   const auto file = c.resolve ? c.resolve(*id) : std::nullopt;
                   if (!file) return fail(ErrorCode::NotFound, std::format("the paint layer {} isn't in the project", id->to_string()));
                   auto paint = terrain::PaintLayer::load(*file);
                   if (!paint) return std::unexpected(paint.error());
                   const auto tx = static_cast<std::uint32_t>(c.integer("tiles_x")), tz = static_cast<std::uint32_t>(c.integer("tiles_z"));
                   if (paint->tiles_x() != tx || paint->tiles_z() != tz)
                       return fail(ErrorCode::InvalidArgument, std::format("the paint layer has {} x {} tiles; this node says {} x {}", paint->tiles_x(),
                                                                           paint->tiles_z(), tx, tz));
                   const auto layer = static_cast<std::uint32_t>(c.integer("layer"));
                   auto f = std::make_shared<Field>();
                   f->sx = tx * kTileCells + 1;
                   f->sz = tz * kTileCells + 1;
                   f->cell = static_cast<float>(c.number("cell_size"));
                   f->v.resize(static_cast<std::size_t>(f->sx) * f->sz);
                   for (std::uint32_t z = 0; z < f->sz; ++z)
                       for (std::uint32_t x = 0; x < f->sx; ++x)
                           f->v[static_cast<std::size_t>(z) * f->sx + x] = terrain::PaintLayer::weight(paint->get(x, z), layer);
                   Value v = field_value(std::move(f));
                   v.unit = Unit::Ratio;
                   return Outputs{{"mask", v}};
               },
               .shape = shape_from_params,
               .extra_key = [](const NodeContext& c) -> std::string {
                   // The paint file's size and time: painting more is a new result.
                   const auto id = Guid::parse(c.choice("paint"));
                   const auto file = id && c.resolve ? c.resolve(*id) : std::nullopt;
                   std::error_code ec;
                   if (!file || !std::filesystem::exists(*file, ec)) return "missing";
                   return std::format("{} {}", std::filesystem::file_size(*file, ec), std::filesystem::last_write_time(*file, ec).time_since_epoch().count());
               }});

        // ---- Scatter (part 7): instance sets ----

        auto exclude_params = size_params();
        exclude_params.push_back(text("paths", "Paths to keep clear: points \"x,z x,z ...\" (m, the terrain's space); several paths separated by \";\""));
        exclude_params.push_back(number("width", 4.0, 0.0, 1e4, "A path's width (m)"));
        exclude_params.push_back(text("circles", "Round areas to keep clear: \"x,z,radius; ...\" (m): houses, clearings"));
        exclude_params.push_back(number("soft", 2.0, 0.0, 1e4, "Fade from 0 to 1 over this distance (m)"));
        r.add({.name = "exclude", .version = 1,
               .help = "A mask that is 0 on paths and in circles and 1 elsewhere (fading over `soft` metres): multiply a scatter's "
                       "mask by it to keep trees off paths and away from houses.",
               .outputs = {{"mask", PinType::Field, Unit::Ratio, false, ""}},
               .params = exclude_params,
               .shape = shape_from_params,
               .run_tile = [](const NodeContext& c, const TileInfo& t) -> Result<std::vector<float>> {
                   auto paths = parse_groups(c.choice("paths"), 2, "paths");
                   if (!paths) return std::unexpected(paths.error());
                   auto circles = parse_groups(c.choice("circles"), 3, "circles");
                   if (!circles) return std::unexpected(circles.error());
                   const double half = 0.5 * c.number("width"), soft = c.number("soft");
                   const double cell = t.shape.cell, ox = -0.5 * cell * (t.shape.samples_x() - 1), oz = -0.5 * cell * (t.shape.samples_z() - 1);
                   std::vector<float> out(kTileSize);
                   for (std::uint32_t z = 0; z < kTileSamples; ++z)
                       for (std::uint32_t x = 0; x < kTileSamples; ++x) {
                           const double px = ox + (t.x0() + x) * cell, pz = oz + (t.z0() + z) * cell;
                           double inside = 1e300;  // distance outside the nearest shape's edge (negative: inside)
                           for (const auto& path : *paths) {
                               for (std::size_t k = 0; k + 1 < path.size(); k += 2) {
                                   const double ax = path[k], az = path[k + 1];
                                   const double bx = k + 3 < path.size() ? path[k + 2] : ax, bz = k + 3 < path.size() ? path[k + 3] : az;
                                   const double dx = bx - ax, dz = bz - az, len2 = dx * dx + dz * dz;
                                   const double u = len2 > 0.0 ? std::clamp(((px - ax) * dx + (pz - az) * dz) / len2, 0.0, 1.0) : 0.0;
                                   const double qx = ax + u * dx - px, qz = az + u * dz - pz;
                                   inside = std::min(inside, std::sqrt(qx * qx + qz * qz) - half);
                                   if (k + 3 >= path.size()) break;
                               }
                           }
                           for (const auto& cs : *circles)
                               for (std::size_t k = 0; k + 2 < cs.size(); k += 3) {
                                   const double dx = px - cs[k], dz = pz - cs[k + 1];
                                   inside = std::min(inside, std::sqrt(dx * dx + dz * dz) - cs[k + 2]);
                               }
                           const double m = inside <= 0.0 ? 0.0 : soft <= 0.0 ? 1.0 : smoothstep(inside / soft);
                           out[static_cast<std::size_t>(z) * kTileSamples + x] = static_cast<float>(m);
                       }
                   return out;
               }});

        r.add({.name = "scatter", .version = 1,
               .help = "Places copies of an asset (a tree, a rock) naturally: no two closer than `spacing`, about `density` per "
                       "100 m² where the mask is 1 (fewer where it's lower, none where it's 0), each turned and sized at random. "
                       "With a height field, over its area and standing on it; otherwise `width` x `depth` metres at height 0. "
                       "Each copy has a stable id, so trees removed by hand stay removed when you change other settings.",
               .inputs = {{"height", PinType::Field, Unit::Metres, true, ""}, {"mask", PinType::Field, Unit::Ratio, true, ""}},
               .outputs = {{"instances", PinType::InstanceSet, Unit::None, false, ""}},
               .params = {text("asset", "The model to place (an asset id)"),
                          number("density", 1.0, 0.0, 10000.0, "Copies per 100 m² where the mask is 1 (at most what the spacing allows)"),
                          number("spacing", 6.0, 0.1, 1e4, "No two copies closer than this (m)"),
                          number("scale_min", 0.85, 0.01, 100.0, "Smallest size"), number("scale_max", 1.15, 0.01, 100.0, "Largest size"),
                          number("width", 200.0, 1.0, 1e5, "Area across (m), without a height field"),
                          number("depth", 200.0, 1.0, 1e5, "Area deep (m), without a height field"),
                          integer("max_count", 100000, 1, 10000000, "Stop with an error beyond this many (a typo in density shouldn't plant millions)")},
               .run = [](const NodeContext& c) -> Result<Outputs> {
                   const auto asset = Guid::parse(c.choice("asset"));
                   if (!asset) return fail(ErrorCode::InvalidArgument, "\"asset\" isn't an asset id: pick the model to place");
                   const double smin = c.number("scale_min"), smax = c.number("scale_max");
                   if (smin > smax) return fail(ErrorCode::InvalidArgument, "scale_min is above scale_max");
                   const Field* height = c.input("height") ? c.input("height")->field.get() : nullptr;
                   const Field* mask = c.input("mask") ? c.input("mask")->field.get() : nullptr;
                   const double width = height ? height->cell * (height->sx - 1) : c.number("width");
                   const double depth = height ? height->cell * (height->sz - 1) : c.number("depth");
                   const double spacing = c.number("spacing");
                   // One candidate per grid cell (half the spacing's square: at most one copy fits), each from its
                   // own random stream: a cell's copy doesn't depend on any other cell.
                   const double g = spacing * 0.70710678118654752;
                   const auto nx = static_cast<std::uint64_t>(std::ceil(width / g)), nz = static_cast<std::uint64_t>(std::ceil(depth / g));
                   if (nx * nz > 400'000'000ull) return fail(ErrorCode::InvalidArgument, "the area is too big for this spacing");
                   const double expected = std::min(1.0, c.number("density") / 100.0 * g * g);  // copies per cell where the mask is 1
                   const std::uint64_t root = c.stream("scatter").next();
                   const auto max_count = static_cast<std::size_t>(c.integer("max_count"));
                   constexpr double kTwoPi = 6.28318530717958647692;
                   std::vector<Instance> out;
                   std::unordered_map<std::uint64_t, std::size_t> grid;  // cell -> accepted copy
                   for (std::uint64_t cz = 0; cz < nz; ++cz) {
                       if (c.cancelled()) return fail(ErrorCode::Cancelled, "cancelled");
                       for (std::uint64_t cx = 0; cx < nx; ++cx) {
                           const std::uint64_t cell = cz * nx + cx;
                           Random rng(mix(root ^ mix(cell + 1)));
                           const double x = -0.5 * width + (static_cast<double>(cx) + rng.uniform()) * g;
                           const double z = -0.5 * depth + (static_cast<double>(cz) + rng.uniform()) * g;
                           const double keep = rng.uniform(), yaw = rng.uniform() * kTwoPi, size = rng.uniform();
                           if (x > 0.5 * width || z > 0.5 * depth) continue;  // the last row and column overhang the area
                           const double m = mask ? std::clamp(sample(*mask, x, z), 0.0, 1.0) : 1.0;
                           if (keep >= expected * m) continue;
                           bool clear = true;  // no accepted copy within `spacing` (they're at most two cells away)
                           for (std::int64_t dz = -2; dz <= 2 && clear; ++dz)
                               for (std::int64_t dx = -2; dx <= 2 && clear; ++dx) {
                                   const std::int64_t qx = static_cast<std::int64_t>(cx) + dx, qz = static_cast<std::int64_t>(cz) + dz;
                                   if (qx < 0 || qz < 0 || qx >= static_cast<std::int64_t>(nx) || qz >= static_cast<std::int64_t>(nz)) continue;
                                   const auto it = grid.find(static_cast<std::uint64_t>(qz) * nx + static_cast<std::uint64_t>(qx));
                                   if (it == grid.end()) continue;
                                   const auto& o = out[it->second];
                                   const double ex = o.position.x - x, ez = o.position.z - z;
                                   clear = ex * ex + ez * ez >= spacing * spacing;
                               }
                           if (!clear) continue;
                           if (out.size() >= max_count)
                               return fail(ErrorCode::InvalidArgument, std::format("more than {} copies: lower the density or raise max_count", max_count));
                           const auto [s, co] = sin_cos(0.5 * yaw);
                           Instance inst;
                           inst.asset = *asset;
                           inst.position = {static_cast<float>(x), height ? static_cast<float>(sample(*height, x, z)) : 0.0f, static_cast<float>(z)};
                           inst.rotation = {0.0f, static_cast<float>(s), 0.0f, static_cast<float>(co)};
                           inst.scale = static_cast<float>(smin + (smax - smin) * size);
                           inst.seed = mix(root ^ mix(cell + 0x5bd1e995ull));  // the copy's stable id
                           grid.emplace(cell, out.size());
                           out.push_back(inst);
                       }
                   }
                   return Outputs{{"instances", instances_value(std::move(out))}};
               }});

        r.add({.name = "merge", .version = 1,
               .help = "Two instance sets as one (oaks and rocks): `b`'s copies closer than `spacing` to one of `a`'s are left out, "
                       "so `a` wins where they meet.",
               .inputs = {{"a", PinType::InstanceSet, Unit::None, false, ""}, {"b", PinType::InstanceSet, Unit::None, false, ""}},
               .outputs = {{"instances", PinType::InstanceSet, Unit::None, false, ""}},
               .params = {number("spacing", 2.0, 0.0, 1e4, "Keep b's copies at least this far from a's (m; 0: keep all)")},
               .run = [](const NodeContext& c) -> Result<Outputs> {
                   const auto& a = *c.input("a")->instances;
                   const auto& b = *c.input("b")->instances;
                   const double s = c.number("spacing");
                   std::vector<Instance> out(a.begin(), a.end());
                   if (s <= 0.0) {
                       out.insert(out.end(), b.begin(), b.end());
                   } else {
                       std::unordered_map<std::uint64_t, std::vector<std::size_t>> grid;  // cells of `s` metres
                       const auto key = [s](double x, double z) {
                           return (static_cast<std::uint64_t>(static_cast<std::int64_t>(std::floor(x / s)) & 0xffffffff) << 32) |
                                  static_cast<std::uint64_t>(static_cast<std::int64_t>(std::floor(z / s)) & 0xffffffff);
                       };
                       for (std::size_t i = 0; i < a.size(); ++i) grid[key(a[i].position.x, a[i].position.z)].push_back(i);
                       for (const auto& inst : b) {
                           bool clear = true;
                           for (int dz = -1; dz <= 1 && clear; ++dz)
                               for (int dx = -1; dx <= 1 && clear; ++dx) {
                                   const auto it = grid.find(key(inst.position.x + dx * s, inst.position.z + dz * s));
                                   if (it == grid.end()) continue;
                                   for (const std::size_t i : it->second) {
                                       const double ex = a[i].position.x - inst.position.x, ez = a[i].position.z - inst.position.z;
                                       if (ex * ex + ez * ez < s * s) clear = false;
                                   }
                               }
                           if (clear) out.push_back(inst);
                       }
                   }
                   return Outputs{{"instances", instances_value(std::move(out))}};
               }});
        return r;
    }();
    return registry;
}

} // namespace noor::procgraph
