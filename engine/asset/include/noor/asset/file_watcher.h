#pragma once
// Polling file watcher for hot reload (spec 7.4 "asset changes: safe incremental reimport").
//
// Polling (size + last-write time) works on every OS, needs no threads and is cheap for the
// handful of files an editor has open. A change is reported only after the file has looked the
// same for `settle_polls` consecutive polls (the poll that first sees the change counts as one),
// so a generator still writing a GLB is never read half-written.

#include <cstdint>
#include <filesystem>
#include <vector>

namespace noor::asset {

class FileWatcher {
public:
    explicit FileWatcher(int settle_polls = 2) : settle_polls_(settle_polls) {}

    void watch(const std::filesystem::path& path);
    void unwatch(const std::filesystem::path& path);

    // Call regularly (e.g. every 250–500 ms). Returns files whose new content has settled since
    // the last report. Deleted files are reported once when they come back.
    [[nodiscard]] std::vector<std::filesystem::path> poll();

private:
    struct Stamp {
        std::uintmax_t size = 0;
        std::filesystem::file_time_type time{};
        bool exists = false;
        friend bool operator==(const Stamp&, const Stamp&) = default;
    };
    struct Entry {
        std::filesystem::path path;
        Stamp reported;   // last state handed to the caller
        Stamp candidate;  // state currently settling
        int stable_polls = 0;
    };
    static Stamp stamp(const std::filesystem::path& path);

    std::vector<Entry> entries_;
    int settle_polls_;
};

} // namespace noor::asset
