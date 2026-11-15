#include "noor/foundation/log.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <ostream>  // doctest stringifies std::string_view via operator<<
#include <thread>
#include <doctest/doctest.h>

namespace {

// Installs a memory sink for the duration of a test and restores the logger afterwards.
struct ScopedMemorySink {
    std::shared_ptr<noor::MemoryLogSink> sink = std::make_shared<noor::MemoryLogSink>();
    noor::LogLevel previous = noor::log::min_level();
    ScopedMemorySink() { noor::log::add_sink(sink); }
    ~ScopedMemorySink() {
        noor::log::remove_sink(sink);
        noor::log::set_min_level(previous);
    }
};

} // namespace

TEST_CASE("log: records carry level, category, formatted message and source location") {
    ScopedMemorySink capture;
    NOOR_LOG_INFO("Test", "answer is {}", 42);

    REQUIRE(capture.sink->records.size() == 1);
    const auto& r = capture.sink->records.front();
    CHECK(r.level == noor::LogLevel::Info);
    CHECK(r.category == "Test");
    CHECK(r.message == "answer is 42");
    CHECK(std::string_view(r.where.file_name()).ends_with("test_log.cpp"));
    CHECK(r.where.line() > 0);
}

TEST_CASE("log: records below the minimum level are dropped") {
    ScopedMemorySink capture;
    noor::log::set_min_level(noor::LogLevel::Warn);
    NOOR_LOG_DEBUG("Test", "hidden");
    NOOR_LOG_INFO("Test", "hidden too");
    NOOR_LOG_WARN("Test", "visible");
    NOOR_LOG_ERROR("Test", "also visible");

    REQUIRE(capture.sink->records.size() == 2);
    CHECK(capture.sink->records[0].message == "visible");
    CHECK(capture.sink->records[1].level == noor::LogLevel::Error);
}

TEST_CASE("log: per-level counters increase") {
    ScopedMemorySink capture;
    const auto before = noor::log::count(noor::LogLevel::Warn);
    NOOR_LOG_WARN("Test", "one");
    NOOR_LOG_WARN("Test", "two");
    CHECK(noor::log::count(noor::LogLevel::Warn) == before + 2);
}

TEST_CASE("log: formatted line contains timestamp, level, category and file") {
    ScopedMemorySink capture;
    NOOR_LOG_ERROR("Render", "boom");
    REQUIRE(capture.sink->records.size() == 1);
    const std::string line = noor::format_log_line(capture.sink->records.front());
    CHECK(line.find("[ERROR]") != std::string::npos);
    CHECK(line.find("[Render]") != std::string::npos);
    CHECK(line.find("boom") != std::string::npos);
    CHECK(line.find("test_log.cpp:") != std::string::npos);
    CHECK(line.find('Z') == 23);  // "YYYY-MM-DD HH:MM:SS.mmm" then 'Z' (UTC)
}

TEST_CASE("log: removed sinks receive nothing") {
    auto sink = std::make_shared<noor::MemoryLogSink>();
    noor::log::add_sink(sink);
    noor::log::remove_sink(sink);
    NOOR_LOG_INFO("Test", "nobody hears this");
    CHECK(sink->records.empty());
}

TEST_CASE("log: the file sink writes routine lines to disk within about a second") {
    const auto path = std::filesystem::temp_directory_path() / "noor_log_flush_test.log";
    auto sink = std::make_shared<noor::FileLogSink>(path.string());
    REQUIRE(sink->is_open());
    noor::log::add_sink(sink);
    NOOR_LOG_INFO("Test", "first line");
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    NOOR_LOG_INFO("Test", "second line");  // more than a second later: triggers the flush
    std::ifstream in(path);
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(content.find("first line") != std::string::npos);
    CHECK(content.find("second line") != std::string::npos);
    noor::log::remove_sink(sink);
}
