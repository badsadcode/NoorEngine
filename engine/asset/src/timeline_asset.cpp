#include "noor/asset/timeline_asset.h"

#include "noor/foundation/file_io.h"

#include <algorithm>
#include <format>

namespace noor::asset {

namespace {
Error bad(std::string message) { return Error{ErrorCode::ParseError, std::move(message)}; }
} // namespace

Result<Timeline> parse_timeline(const Json& j) {
    if (!j.is_object() || j.value("noor_timeline", 0) != kTimelineFormat) return std::unexpected(bad("not a NoorEngine timeline (\"noor_timeline\": 1)"));
    Timeline t;
    t.name = j.value("name", std::string("Timeline"));
    t.length = j.contains("length") && j["length"].is_number() ? std::max(0.0, j["length"].get<double>()) : 1.0;
    t.loop = j.value("loop", false);
    if (j.contains("tracks") && !j["tracks"].is_array()) return std::unexpected(bad("\"tracks\" must be a list"));
    int index = 0;
    for (const Json& tj : j.value("tracks", Json::array())) {
        ++index;
        if (!tj.is_object()) return std::unexpected(bad(std::format("track {} isn't an object", index)));
        TimelineTrack track;
        track.target = tj.value("target", std::string());
        track.kind = tj.value("call", false) ? TimelineTrack::Kind::Call : tj.value("emit", false) ? TimelineTrack::Kind::Emit : TimelineTrack::Kind::Value;
        track.property = tj.value("property", std::string());
        if (track.kind == TimelineTrack::Kind::Value && track.property.empty())
            return std::unexpected(bad(std::format("track {} has no \"property\" (or \"call\" / \"emit\": true)", index)));
        for (const Json& kj : tj.value("keys", Json::array())) {
            if (!kj.is_object() || !kj.contains("t") || !kj["t"].is_number())
                return std::unexpected(bad(std::format("track {}: every key needs a time \"t\"", index)));
            TimelineKey k;
            k.t = std::max(0.0, kj["t"].get<double>());
            switch (track.kind) {
            case TimelineTrack::Kind::Value:
                if (!kj.contains("v")) return std::unexpected(bad(std::format("track {}: a key at {} has no value \"v\"", index, k.t)));
                k.value = kj["v"];
                k.ease = kj.value("ease", std::string());
                break;
            case TimelineTrack::Kind::Call:
                k.function = kj.value("function", std::string());
                if (kj.contains("args") && kj["args"].is_array()) k.args = kj["args"];
                break;
            case TimelineTrack::Kind::Emit:
                k.message = kj.value("message", std::string());
                k.data = kj.contains("data") ? kj["data"] : Json();
                break;
            }
            track.keys.push_back(std::move(k));
        }
        sort_keys(track);
        t.tracks.push_back(std::move(track));
    }
    return t;
}

Json timeline_file_json(const Timeline& t) {
    Json tracks = Json::array();
    for (const auto& track : t.tracks) {
        Json tj = {{"target", track.target}};
        if (track.kind == TimelineTrack::Kind::Value) tj["property"] = track.property;
        if (track.kind == TimelineTrack::Kind::Call) tj["call"] = true;
        if (track.kind == TimelineTrack::Kind::Emit) tj["emit"] = true;
        Json keys = Json::array();
        for (const auto& k : track.keys) {
            Json kj = {{"t", k.t}};
            if (track.kind == TimelineTrack::Kind::Value) {
                kj["v"] = k.value;
                if (!k.ease.empty() && k.ease != "linear") kj["ease"] = k.ease;
            } else if (track.kind == TimelineTrack::Kind::Call) {
                kj["function"] = k.function;
                kj["args"] = k.args;
            } else {
                kj["message"] = k.message;
                if (!k.data.is_null()) kj["data"] = k.data;
            }
            keys.push_back(std::move(kj));
        }
        tj["keys"] = std::move(keys);
        tracks.push_back(std::move(tj));
    }
    return {{"noor_timeline", kTimelineFormat}, {"name", t.name}, {"length", t.length}, {"loop", t.loop}, {"tracks", std::move(tracks)}};
}

Result<Timeline> load_timeline_file(const std::filesystem::path& path) {
    auto text = fs::read_file(path, 16u << 20);
    if (!text) return std::unexpected(text.error());
    auto j = parse_json(*text, path.generic_string());
    if (!j) return std::unexpected(j.error());
    auto t = parse_timeline(*j);
    if (!t) return std::unexpected(Error{t.error().code, std::format("{}: {}", path.generic_string(), t.error().message)});
    return t;
}

std::optional<KeySpan> key_span(const TimelineTrack& track, double t) {
    const auto& k = track.keys;
    if (k.empty()) return std::nullopt;
    if (t <= k.front().t) return KeySpan{&k.front(), &k.front(), 0.0};
    if (t >= k.back().t) return KeySpan{&k.back(), &k.back(), 0.0};
    // The last key at or before t, and the next one.
    const auto next = std::ranges::upper_bound(k, t, {}, &TimelineKey::t);
    const auto& b = *next;
    const auto& a = *(next - 1);
    const double span = b.t - a.t;
    return KeySpan{&a, &b, span > 0.0 ? (t - a.t) / span : 1.0};
}

void sort_keys(TimelineTrack& track) { std::ranges::stable_sort(track.keys, {}, &TimelineKey::t); }

} // namespace noor::asset
