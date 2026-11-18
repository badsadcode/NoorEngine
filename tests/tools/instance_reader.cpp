// A tiny standalone reader for instance set files (Milestone 7, part 7): proof that a generated forest
// is usable outside the engine and its generator. It links no engine code at all, only a JSON
// parser, and follows the format exactly as documented (docs/procgraph/PROCGRAPH.md):
//
//   noor_instance_reader <file.instances.json>
//   -> "<n> instances of <a> asset(s); x <min>..<max>, z <min>..<max>; ids unique: yes"
//
// Exit code 1 if the file doesn't follow the format.
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: noor_instance_reader <file.instances.json>\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "can't open %s\n", argv[1]);
        return 1;
    }
    std::stringstream text;
    text << in.rdbuf();
    const auto j = nlohmann::json::parse(text.str(), nullptr, false);
    if (j.is_discarded() || !j.is_object() || j.value("noor_instances", 0) != 1) {
        std::fprintf(stderr, "not an instance set (format 1)\n");
        return 1;
    }
    const auto& assets = j["assets"];
    const auto& rows = j["instances"];
    if (!assets.is_array() || !rows.is_array()) {
        std::fprintf(stderr, "\"assets\" and \"instances\" must be lists\n");
        return 1;
    }
    double x0 = 1e300, x1 = -1e300, z0 = 1e300, z1 = -1e300;
    std::set<std::string> ids;
    for (const auto& r : rows) {
        // [asset, id, x, y, z, qx, qy, qz, qw, scale]
        if (!r.is_array() || r.size() != 10 || !r[0].is_number_unsigned() || r[0].get<std::size_t>() >= assets.size() || !r[1].is_string()) {
            std::fprintf(stderr, "a row doesn't follow [asset, id, x, y, z, qx, qy, qz, qw, scale]\n");
            return 1;
        }
        const double x = r[2].get<double>(), z = r[4].get<double>();
        const double qx = r[5].get<double>(), qy = r[6].get<double>(), qz = r[7].get<double>(), qw = r[8].get<double>();
        if (std::abs(qx * qx + qy * qy + qz * qz + qw * qw - 1.0) > 1e-3 || r[9].get<double>() <= 0.0) {
            std::fprintf(stderr, "a rotation isn't a unit quaternion or a scale isn't positive\n");
            return 1;
        }
        x0 = std::min(x0, x), x1 = std::max(x1, x), z0 = std::min(z0, z), z1 = std::max(z1, z);
        ids.insert(r[1].get<std::string>());
    }
    std::printf("%zu instances of %zu asset(s); x %.1f..%.1f, z %.1f..%.1f; ids unique: %s\n", rows.size(), assets.size(), x0, x1, z0, z1,
                ids.size() == rows.size() ? "yes" : "no");
    return ids.size() == rows.size() ? 0 : 1;
}
