#include "noor/asset/file_watcher.h"

#include <algorithm>

namespace noor::asset {

FileWatcher::Stamp FileWatcher::stamp(const std::filesystem::path& path) {
    std::error_code ec;
    Stamp s;
    s.exists = std::filesystem::is_regular_file(path, ec);
    if (s.exists) {
        s.size = std::filesystem::file_size(path, ec);
        s.time = std::filesystem::last_write_time(path, ec);
    }
    return s;
}

void FileWatcher::watch(const std::filesystem::path& path) {
    const auto it = std::ranges::find(entries_, path, &Entry::path);
    if (it != entries_.end()) return;
    const Stamp now = stamp(path);
    entries_.push_back({path, now, now, 0});
}

void FileWatcher::unwatch(const std::filesystem::path& path) {
    std::erase_if(entries_, [&](const Entry& e) { return e.path == path; });
}

std::vector<std::filesystem::path> FileWatcher::poll() {
    std::vector<std::filesystem::path> changed;
    for (Entry& e : entries_) {
        const Stamp now = stamp(e.path);
        if (now != e.candidate) {  // changed (or still changing): this poll is the first sighting
            e.candidate = now;
            e.stable_polls = 1;
            continue;
        }
        if (now == e.reported) continue;
        if (++e.stable_polls >= settle_polls_) {
            e.reported = now;
            e.stable_polls = 0;
            if (now.exists) changed.push_back(e.path);
        }
    }
    return changed;
}

} // namespace noor::asset
