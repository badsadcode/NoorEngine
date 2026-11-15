#pragma once
// Structured, thread-safe logging.
//
// Usage:
//   NOOR_LOG_INFO("Render", "Created swapchain {}x{}", w, h);
//
// Records carry level, category, message, timestamp, thread id and source
// location. Output goes to any number of sinks (console, file, memory...).

#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace noor {

enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warn, Error, Fatal };

[[nodiscard]] std::string_view to_string(LogLevel level) noexcept;

struct LogRecord {
    LogLevel level = LogLevel::Info;
    std::string category;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
    std::thread::id thread;
    std::source_location where;
};

// Formats a record as a single line (UTC timestamp):
// "2026-11-15 13:22:01.123Z [INFO ] [Render] [t1234] message (file.cpp:42)".
[[nodiscard]] std::string format_log_line(const LogRecord& record);

class LogSink {
public:
    virtual ~LogSink() = default;
    // Called with the logger's lock held; implementations need not be thread-safe themselves.
    virtual void write(const LogRecord& record) = 0;
    virtual void flush() {}
};

// Colored output to stderr.
class ConsoleLogSink final : public LogSink {
public:
    void write(const LogRecord& record) override;
};

// Appends formatted lines to a file. Flushes on Warn and above so crashes keep context.
class FileLogSink final : public LogSink {
public:
    explicit FileLogSink(const std::string& path);
    ~FileLogSink() override;
    [[nodiscard]] bool is_open() const noexcept;
    void write(const LogRecord& record) override;
    void flush() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Keeps records in memory. Intended for tests and in-editor consoles.
class MemoryLogSink final : public LogSink {
public:
    void write(const LogRecord& record) override { records.push_back(record); }
    std::vector<LogRecord> records;
};

namespace log {

void add_sink(std::shared_ptr<LogSink> sink);
void remove_sink(const std::shared_ptr<LogSink>& sink);
void clear_sinks();
void set_min_level(LogLevel level) noexcept;
[[nodiscard]] LogLevel min_level() noexcept;
void flush();

// Number of records written at each level since startup (useful for "zero errors" checks).
[[nodiscard]] std::uint64_t count(LogLevel level) noexcept;

void write_record(LogLevel level, std::string_view category, std::string message,
                  std::source_location where);

template <class... Args>
void write(LogLevel level, std::string_view category, std::source_location where,
           std::format_string<Args...> fmt, Args&&... args) {
    if (level < min_level()) {
        return;
    }
    write_record(level, category, std::format(fmt, std::forward<Args>(args)...), where);
}

} // namespace log
} // namespace noor

#define NOOR_LOG_(lvl, category, ...) \
    ::noor::log::write(::noor::LogLevel::lvl, category, std::source_location::current(), __VA_ARGS__)

#define NOOR_LOG_TRACE(category, ...) NOOR_LOG_(Trace, category, __VA_ARGS__)
#define NOOR_LOG_DEBUG(category, ...) NOOR_LOG_(Debug, category, __VA_ARGS__)
#define NOOR_LOG_INFO(category, ...)  NOOR_LOG_(Info, category, __VA_ARGS__)
#define NOOR_LOG_WARN(category, ...)  NOOR_LOG_(Warn, category, __VA_ARGS__)
#define NOOR_LOG_ERROR(category, ...) NOOR_LOG_(Error, category, __VA_ARGS__)
#define NOOR_LOG_FATAL(category, ...) NOOR_LOG_(Fatal, category, __VA_ARGS__)
