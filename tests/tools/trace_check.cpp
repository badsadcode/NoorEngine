// noor_trace_check: judges a walk into a weather zone (Milestone 8A, part 7) from noor_player's
// --trace-environment report.
//
//   noor_trace_check <report.json> [<other report.json>] --fog-from A --fog-to B [--collect-max-ms M]
//
// Passes when, frame by frame, the fog density goes from A to B without ever going back (the camera walks in), no
// frame moves it more than 3% of the way, the exposure never jumps more than 0.1 stop between frames, the zone ends
// fully on, and (with a second report) both traces agree frame by frame: a built game blends exactly like the
// project. With --collect-max-ms, 95% of either run's frames must spend under M ms gathering the scene and none over 120 ms (game::collect_view,
// the report's timing_ms.collect.max): a blend must never rebuild an atmosphere's tables mid-walk. Prints what it found. Exit code: 0 pass, 1 fail, 2 usage or file error.
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

using nlohmann::json;

namespace {

bool load(const char* path, json& out) {
    std::ifstream in(path);
    if (!in) return false;
    try {
        in >> out;
    } catch (const std::exception&) {
        return false;
    }
    return out.contains("environment_trace") && out["environment_trace"].is_array() && !out["environment_trace"].empty();
}

} // namespace

int main(int argc, char** argv) {
    const char* paths[2] = {nullptr, nullptr};
    double from = 0.0, to = 0.0, collect_max = -1.0, rain_to = -1.0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--fog-from" && i + 1 < argc) from = std::atof(argv[++i]);
        else if (a == "--fog-to" && i + 1 < argc) to = std::atof(argv[++i]);
        else if (a == "--collect-max-ms" && i + 1 < argc) collect_max = std::atof(argv[++i]);
        else if (a == "--rain-to" && i + 1 < argc) rain_to = std::atof(argv[++i]);
        else if (!paths[0]) paths[0] = argv[i];
        else if (!paths[1]) paths[1] = argv[i];
        else return std::fprintf(stderr, "usage: noor_trace_check <report.json> [<other.json>] --fog-from A --fog-to B\n"), 2;
    }
    json a, b;
    if (!paths[0] || !load(paths[0], a)) return std::fprintf(stderr, "can't read a trace from %s\n", paths[0] ? paths[0] : "(none)"), 2;
    if (paths[1] && !load(paths[1], b)) return std::fprintf(stderr, "can't read a trace from %s\n", paths[1]), 2;
    const auto& trace = a["environment_trace"];
    const double range = std::abs(to - from);
    bool ok = true;
    double biggest_fog_step = 0.0, biggest_ev_step = 0.0;
    std::size_t backwards = 0;
    for (std::size_t i = 1; i < trace.size(); ++i) {
        const double f0 = trace[i - 1]["fog_density"], f1 = trace[i]["fog_density"];
        const double e0 = trace[i - 1]["ev100"], e1 = trace[i]["ev100"];
        biggest_fog_step = std::max(biggest_fog_step, std::abs(f1 - f0));
        biggest_ev_step = std::max(biggest_ev_step, std::abs(e1 - e0));
        if ((to - from) * (f1 - f0) < -1e-9) ++backwards;
    }
    const double first = trace.front()["fog_density"], last = trace.back()["fog_density"];
    const double weight = trace.back()["zones"].empty() ? 0.0 : trace.back()["zones"][0].get<double>();
    std::printf("%zu frames: fog %.5f -> %.5f (wanted %.5f -> %.5f), biggest step %.5f (%.1f%% of the way), exposure step %.3f EV, "
                "zone at the end %.2f\n",
                trace.size(), first, last, from, to, biggest_fog_step, range > 0 ? 100.0 * biggest_fog_step / range : 0.0, biggest_ev_step,
                weight);
    if (std::abs(first - from) > range * 0.01 + 1e-9 || std::abs(last - to) > range * 0.01 + 1e-9) {
        std::printf("FAIL: the walk doesn't start and end where it should\n");
        ok = false;
    }
    if (backwards > 0) {
        std::printf("FAIL: the fog went back %zu time(s) while walking in\n", backwards);
        ok = false;
    }
    if (biggest_fog_step > range * 0.03 + 1e-9) {
        std::printf("FAIL: a frame moved the fog more than 3%% of the way: not smooth\n");
        ok = false;
    }
    if (biggest_ev_step > 0.1) {
        std::printf("FAIL: the exposure jumped more than 0.1 stop in a frame\n");
        ok = false;
    }
    if (weight < 0.999) {
        std::printf("FAIL: the zone isn't fully on at the end\n");
        ok = false;
    }
    if (rain_to >= 0.0) {  // 8B part 3: the rain starts smoothly too (from none to rain_to, never back, no jumps)
        double biggest = 0.0, previous = 0.0;
        std::size_t back = 0;
        for (std::size_t i = 0; i < trace.size(); ++i) {
            const double r = trace[i].value("rain", -1.0);
            if (r < 0.0) {
                std::printf("FAIL: the trace has no rain\n");
                ok = false;
                break;
            }
            if (i > 0) {
                biggest = std::max(biggest, std::abs(r - previous));
                if (r < previous - 1e-9) ++back;
            }
            previous = r;
        }
        std::printf("rain: %.3f -> %.3f, biggest step %.4f, went back %zu time(s)\n", trace.front().value("rain", 0.0), previous, biggest, back);
        if (trace.front().value("rain", 1.0) > 0.01 || previous < rain_to - 0.01 || back > 0 || biggest > rain_to * 0.03 + 1e-9) {
            std::printf("FAIL: the rain doesn't start smoothly\n");
            ok = false;
        }
    }
    if (collect_max >= 0.0) {
        for (const json* r : {&a, paths[1] ? &b : nullptr}) {
            if (!r) continue;
            const auto* c = r->contains("timing_ms") && (*r)["timing_ms"].contains("collect") ? &(*r)["timing_ms"]["collect"] : nullptr;
            if (!c) {
                std::printf("FAIL: a report has no timing_ms.collect\n");
                ok = false;
                continue;
            }
            // p95 catches trouble every frame; the worst frame may be the OS stealing the core for a moment (seen at
            // 46 ms with six tests in parallel), but never as long as one atmosphere build (about 240 ms).
            const double worst = (*c)["max"], median = (*c)["median"], p95 = (*c)["p95"];
            const double worst_allowed = std::max(120.0, collect_max);
            const double wall = r->contains("timing_ms") && (*r)["timing_ms"].contains("wall") ? (*r)["timing_ms"]["wall"]["median"].get<double>() : 0.0;
            std::printf("gathering the scene: median %.2f ms, p95 %.2f ms (allowed %.1f), worst %.2f ms (allowed %.0f); frame median %.2f ms\n",
                        median, p95, collect_max, worst, worst_allowed, wall);
            if (p95 > collect_max || worst > worst_allowed) {
                std::printf("FAIL: frames spent too long gathering the scene (an atmosphere rebuilt mid-walk?)\n");
                ok = false;
            }
        }
    }
    if (paths[1]) {
        const auto& other = b["environment_trace"];
        std::size_t differ = 0;
        if (other.size() != trace.size()) {
            std::printf("FAIL: the traces have %zu and %zu frames\n", trace.size(), other.size());
            ok = false;
        } else {
            for (std::size_t i = 0; i < trace.size(); ++i) {
                const double fa = trace[i]["fog_density"], fb = other[i]["fog_density"];
                const double ea = trace[i]["ev100"], eb = other[i]["ev100"];
                if (std::abs(fa - fb) > 1e-7 || std::abs(ea - eb) > 1e-4) ++differ;
            }
            std::printf("the second trace differs in %zu of %zu frames\n", differ, trace.size());
            if (differ > 0) ok = false;
        }
    }
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
