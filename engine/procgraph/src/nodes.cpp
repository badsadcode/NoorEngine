// The built-in nodes (Milestone 7, part 1). Every node is pure: its outputs depend only on its
// parameters, its seed stream and its inputs. Bump a node's version whenever its results change,
// or cached results (part 2) would be served stale.
#include "noor/procgraph/procgraph.h"

#include "noor/terrain/terrain.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace noor::procgraph {
using asset::Json;

namespace {

using Kind = ParamSpec::Kind;

ParamSpec number(std::string name, double fallback, double min, double max, std::string help) {
    return {Kind::Number, std::move(name), fallback, min, max, {}, std::move(help)};
}
ParamSpec integer(std::string name, std::int64_t fallback, double min, double max, std::string help) {
    return {Kind::Integer, std::move(name), fallback, min, max, {}, std::move(help)};
}
ParamSpec choice(std::string name, std::string fallback, std::vector<std::string> choices, std::string help) {
    return {Kind::Choice, std::move(name), fallback, 0, 0, std::move(choices), std::move(help)};
}

Unit unit_from(const std::string& s) { return s == "metres" ? Unit::Metres : s == "ratio" ? Unit::Ratio : s == "degrees" ? Unit::Degrees : Unit::None; }

Value scalar(double v) {
    Value out;
    out.type = PinType::Scalar;
    out.scalar = v;
    return out;
}

Value field(std::shared_ptr<Field> f) {
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

} // namespace

const Registry& Registry::builtin() {
    static const Registry registry = [] {
        Registry r;

        r.add({"constant", 1, "A number, optionally with a unit.", {},
               {{"value", PinType::Scalar, Unit::Any, false, ""}},  // the unit parameter decides
               {number("value", 0.0, -1e12, 1e12, "The number"), choice("unit", "none", {"none", "metres", "ratio", "degrees"}, "Its unit")},
               {},
               [](const NodeContext& c) -> Result<Outputs> {
                   Value v = scalar(c.number("value"));
                   v.unit = unit_from(c.choice("unit"));
                   return Outputs{{"value", v}};
               }});

        r.add({"math", 1, "Two numbers combined: add, subtract, multiply, divide (by zero gives 0), min or max.",
               {{"a", PinType::Scalar, Unit::Any, false, ""}, {"b", PinType::Scalar, Unit::Any, false, ""}},
               {{"out", PinType::Scalar, Unit::SameAs, false, "a"}},
               {choice("op", "add", kOps, "What to do")},
               {"a", "b"},
               [](const NodeContext& c) -> Result<Outputs> {
                   return Outputs{{"out", scalar(apply(c.choice("op"), c.input("a")->scalar, c.input("b")->scalar))}};
               }});

        r.add({"noise", 1,
               "Rolling hills: the terrain's deterministic fractal noise (integer hashing, + - * / only), "
               "heights from 0 to `height` metres, features about `scale` metres across.",
               {},
               {{"height", PinType::Field, Unit::Metres, false, ""}},
               {integer("tiles_x", 1, 1, 16, "Tiles along x (256 cells each)"), integer("tiles_z", 1, 1, 16, "Tiles along z"),
                number("cell_size", 1.0, 0.05, 64.0, "Metres between samples"), number("height", 40.0, 0.0, 10000.0, "Highest point (m)"),
                number("scale", 250.0, 1.0, 100000.0, "Size of the largest hills (m)"), integer("octaves", 5, 1, 12, "Detail layers")},
               {},
               [](const NodeContext& c) -> Result<Outputs> {
                   auto f = std::make_shared<Field>();
                   f->sx = static_cast<std::uint32_t>(c.integer("tiles_x")) * terrain::kTileCells + 1;
                   f->sz = static_cast<std::uint32_t>(c.integer("tiles_z")) * terrain::kTileCells + 1;
                   f->cell = static_cast<float>(c.number("cell_size"));
                   const double scale = c.number("scale"), height = c.number("height");
                   const auto octaves = static_cast<std::uint32_t>(c.integer("octaves"));
                   const std::uint64_t seed = c.stream("noise").next();
                   f->v.resize(static_cast<std::size_t>(f->sx) * f->sz);
                   // The same sums as terrain::Heights::build, so a terrain with this seed matches bit for bit.
                   for (std::uint32_t z = 0; z < f->sz; ++z) {
                       if (c.cancelled()) return fail(ErrorCode::Cancelled, "cancelled");  // a row at a time
                       for (std::uint32_t x = 0; x < f->sx; ++x) {
                           const double px = static_cast<double>(x) * f->cell / scale, pz = static_cast<double>(z) * f->cell / scale;
                           f->v[static_cast<std::size_t>(z) * f->sx + x] = (terrain::fractal_noise(px, pz, seed, octaves) * 0.5f + 0.5f) * static_cast<float>(height);
                       }
                   }
                   return Outputs{{"height", field(std::move(f))}};
               }});

        r.add({"combine", 1,
               "Two fields sample by sample: add, subtract, multiply, divide, min, max, or lerp (a to b by mask t). "
               "Both must be the same size and unit.",
               {{"a", PinType::Field, Unit::Any, false, ""}, {"b", PinType::Field, Unit::Any, false, ""}, {"t", PinType::Field, Unit::Ratio, true, ""}},
               {{"out", PinType::Field, Unit::SameAs, false, "a"}},
               {choice("op", "add", {"add", "subtract", "multiply", "divide", "min", "max", "lerp"}, "What to do")},
               {"a", "b"},
               [](const NodeContext& c) -> Result<Outputs> {
                   const Field& a = *c.input("a")->field;
                   const Field& b = *c.input("b")->field;
                   const Field* t = c.input("t") ? c.input("t")->field.get() : nullptr;
                   if (a.sx != b.sx || a.sz != b.sz || (t && (t->sx != a.sx || t->sz != a.sz)))
                       return fail(ErrorCode::InvalidArgument, std::format("the fields are different sizes ({}x{}, {}x{}{})", a.sx, a.sz, b.sx, b.sz,
                                                                            t ? std::format(", t {}x{}", t->sx, t->sz) : std::string()));
                   const std::string op = c.choice("op");
                   if (op == "lerp" && !t) return fail(ErrorCode::InvalidArgument, "lerp needs the mask input \"t\"");
                   auto f = std::make_shared<Field>(a);
                   for (std::size_t i = 0; i < f->v.size(); ++i)
                       f->v[i] = op == "lerp" ? a.v[i] + (b.v[i] - a.v[i]) * std::clamp(t->v[i], 0.0f, 1.0f)
                                              : static_cast<float>(apply(op, a.v[i], b.v[i]));
                   return Outputs{{"out", field(std::move(f))}};
               }});

        r.add({"clamp", 1, "Keeps every sample between min and max.",
               {{"in", PinType::Field, Unit::Any, false, ""}},
               {{"out", PinType::Field, Unit::SameAs, false, "in"}},
               {number("min", 0.0, -1e12, 1e12, "Lowest"), number("max", 1.0, -1e12, 1e12, "Highest")},
               {},
               [](const NodeContext& c) -> Result<Outputs> {
                   const float lo = static_cast<float>(c.number("min")), hi = static_cast<float>(c.number("max"));
                   if (lo > hi) return fail(ErrorCode::InvalidArgument, std::format("min ({}) is above max ({})", lo, hi));
                   auto f = std::make_shared<Field>(*c.input("in")->field);
                   for (float& v : f->v) v = std::clamp(v, lo, hi);
                   return Outputs{{"out", field(std::move(f))}};
               }});

        r.add({"remap", 1,
               "Maps from_min..from_max onto to_min..to_max (straight line, clamped at the ends). Its output unit: "
               "\"ratio\" turns heights into a mask, \"metres\" a mask into heights, \"same\" keeps the input's.",
               {{"in", PinType::Field, Unit::Any, false, ""}},
               {{"out", PinType::Field, Unit::Any, false, ""}},
               {number("from_min", 0.0, -1e12, 1e12, "Input low"), number("from_max", 1.0, -1e12, 1e12, "Input high"),
                number("to_min", 0.0, -1e12, 1e12, "Output low"), number("to_max", 1.0, -1e12, 1e12, "Output high"),
                choice("unit", "same", {"same", "metres", "ratio", "none"}, "The output's unit")},
               {},
               [](const NodeContext& c) -> Result<Outputs> {
                   const double a = c.number("from_min"), b = c.number("from_max"), lo = c.number("to_min"), hi = c.number("to_max");
                   if (a == b) return fail(ErrorCode::InvalidArgument, "from_min and from_max are the same: nothing to map");
                   auto f = std::make_shared<Field>(*c.input("in")->field);
                   for (float& v : f->v) {
                       const double k = std::clamp((static_cast<double>(v) - a) / (b - a), 0.0, 1.0);
                       v = static_cast<float>(lo + (hi - lo) * k);
                   }
                   Value out = field(std::move(f));
                   const std::string u = c.choice("unit");
                   out.unit = u == "same" ? c.input("in")->unit : unit_from(u);
                   return Outputs{{"out", out}};
               }});
        return r;
    }();
    return registry;
}

} // namespace noor::procgraph
