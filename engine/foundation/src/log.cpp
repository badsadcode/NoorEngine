#include "noor/foundation/log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>

namespace noor {

std::string_view to_string(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO ";
    case LogLevel::Warn: return "WARN ";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Fatal: return "FATAL";
    }
    return "?????";
}

namespace {

std::string_view file_name_only(std::string_view path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

std::string thread_id_string(std::thread::id id) {
    std::ostringstream stream;
    stream << id;
    return stream.str();
}

struct LoggerState {
    std::mutex mutex;
    std::vector<std::shared_ptr<LogSink>> sinks;
    std::atomic<LogLevel> min_level{LogLevel::Debug};
    std::array<std::atomic<std::uint64_t>, 6> counts{};
};

LoggerState& state() {
    static LoggerState instance;
    return instance;
}

} // namespace

std::string format_log_line(const LogRecord& record) {
    const auto ms = std::chrono::time_point_cast<std::chrono::milliseconds>(record.timestamp);
    return std::format("{:%Y-%m-%d %H:%M:%S}Z [{}] [{}] [t{}] {} ({}:{})", ms, to_string(record.level),
                       record.category, thread_id_string(record.thread), record.message,
                       file_name_only(record.where.file_name()), record.where.line());
}

// ---------------------------------------------------------------- sinks

void ConsoleLogSink::write(const LogRecord& record) {
    const char* color = "";
    switch (record.level) {
    case LogLevel::Trace:
    case LogLevel::Debug: color = "\x1b[90m"; break;
    case LogLevel::Info: color = "\x1b[0m"; break;
    case LogLevel::Warn: color = "\x1b[33m"; break;
    case LogLevel::Error:
    case LogLevel::Fatal: color = "\x1b[31m"; break;
    }
    const std::string line = format_log_line(record);
    std::fprintf(stderr, "%s%s\x1b[0m\n", color, line.c_str());
}

struct FileLogSink::Impl {
    std::ofstream file;
};

FileLogSink::FileLogSink(const std::string& path) : impl_(std::make_unique<Impl>()) {
    impl_->file.open(path, std::ios::out | std::ios::trunc);
}

FileLogSink::~FileLogSink() = default;

bool FileLogSink::is_open() const noexcept { return impl_->file.is_open(); }

void FileLogSink::write(const LogRecord& record) {
    if (!impl_->file.is_open()) {
        return;
    }
    impl_->file << format_log_line(record) << '\n';
    if (record.level >= LogLevel::Warn) {
        impl_->file.flush();
    }
}

void FileLogSink::flush() {
    if (impl_->file.is_open()) {
        impl_->file.flush();
    }
}

// ---------------------------------------------------------------- logger

namespace log {

void add_sink(std::shared_ptr<LogSink> sink) {
    if (!sink) {
        return;
    }
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    s.sinks.push_back(std::move(sink));
}

void remove_sink(const std::shared_ptr<LogSink>& sink) {
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    std::erase(s.sinks, sink);
}

void clear_sinks() {
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    s.sinks.clear();
}

void set_min_level(LogLevel level) noexcept { state().min_level.store(level); }

LogLevel min_level() noexcept { return state().min_level.load(); }

void flush() {
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    for (auto& sink : s.sinks) {
        sink->flush();
    }
}

std::uint64_t count(LogLevel level) noexcept {
    return state().counts[static_cast<std::size_t>(level)].load();
}

void write_record(LogLevel level, std::string_view category, std::string message,
                  std::source_location where) {
    auto& s = state();
    s.counts[static_cast<std::size_t>(level)].fetch_add(1);

    LogRecord record;
    record.level = level;
    record.category = std::string(category);
    record.message = std::move(message);
    record.timestamp = std::chrono::system_clock::now();
    record.thread = std::this_thread::get_id();
    record.where = where;

    std::scoped_lock lock(s.mutex);
    for (auto& sink : s.sinks) {
        sink->write(record);
    }
}

} // namespace log
} // namespace noor
