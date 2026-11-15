#include "noor_editor_api.h"

#include "api_internal.h"
#include "noor/foundation/log.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace noor::editor_api {

namespace {
thread_local std::string t_last_error;
} // namespace

void set_last_error(std::string message) { t_last_error = std::move(message); }

namespace {

// Forwards engine log records to the editor's callback.
class CallbackSink final : public LogSink {
public:
    void set(noor_log_callback callback, void* user) {
        std::scoped_lock lock(mutex_);
        callback_ = callback;
        user_ = user;
    }
    void write(const LogRecord& record) override {
        std::scoped_lock lock(mutex_);
        if (callback_) {
            callback_(static_cast<std::int32_t>(record.level), record.category.c_str(), record.message.c_str(), user_);
        }
    }

private:
    std::mutex mutex_;
    noor_log_callback callback_ = nullptr;
    void* user_ = nullptr;
};

std::shared_ptr<CallbackSink> callback_sink() {
    static std::once_flag once;
    static std::shared_ptr<CallbackSink> sink;
    std::call_once(once, [] {
        sink = std::make_shared<CallbackSink>();
        log::add_sink(sink);
    });
    return sink;
}

} // namespace
} // namespace noor::editor_api

extern "C" {

NOOR_API uint32_t noor_editor_api_version(void) { return NOOR_EDITOR_API_VERSION; }

NOOR_API const char* noor_last_error(void) { return noor::editor_api::t_last_error.c_str(); }

NOOR_API void noor_set_log_callback(noor_log_callback callback, void* user) {
    noor::editor_api::callback_sink()->set(callback, user);
}

} // extern "C"
