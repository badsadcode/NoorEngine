// C ABI for documents (ADR-025). Thin wrappers: parse/serialize JSON, turn errors into
// noor_last_error(), never let a C++ exception cross the DLL boundary.

#include "noor_editor_api.h"

#include "api_internal.h"
#include "noor/foundation/log.h"
#include "document.h"
#include "noor/scripting/csharp_scripts.h"

#include <exception>
#include <memory>
#include <string>

struct noor_doc {
    std::unique_ptr<noor::editor::Document> doc;
};

namespace {
thread_local std::string t_result;

const char* give(const noor::asset::Json& json) {
    t_result = json.dump();
    return t_result.c_str();
}

int32_t fail_with(const noor::Error& e) {
    noor::editor_api::set_last_error(e.describe());
    return 0;
}

// Runs `fn`, converting any escaping exception into an error result.
template <class Fn, class R>
R guarded(R on_error, Fn&& fn) {
    try {
        return fn();
    } catch (const std::exception& e) {
        noor::editor_api::set_last_error(std::string("internal error: ") + e.what());
    } catch (...) {
        noor::editor_api::set_last_error("internal error");
    }
    return on_error;
}
} // namespace

std::optional<bool> noor_doc_tick_internal(noor_doc* doc, double seconds, const noor::runtime::InputState& input) {
    if (!doc) return std::nullopt;
    try {
        doc->doc->tick(seconds, input);
        return doc->doc->take_mouse_capture_request();
    } catch (const std::exception& e) {
        NOOR_LOG_ERROR("Editor", "Play frame failed: {}", e.what());
        return std::nullopt;
    }
}

extern "C" {

NOOR_API noor_doc* noor_doc_open_project(const char* project_path) {
    return guarded<>(static_cast<noor_doc*>(nullptr), [&]() -> noor_doc* {
        auto d = noor::editor::Document::open(project_path ? project_path : "");
        if (!d) {
            fail_with(d.error());
            return nullptr;
        }
        return new noor_doc{std::move(*d)};
    });
}

NOOR_API noor_doc* noor_doc_create_project(const char* folder, const char* name) {
    return guarded<>(static_cast<noor_doc*>(nullptr), [&]() -> noor_doc* {
        auto d = noor::editor::Document::create(folder ? folder : "", name ? name : "");
        if (!d) {
            fail_with(d.error());
            return nullptr;
        }
        return new noor_doc{std::move(*d)};
    });
}

NOOR_API void noor_doc_close(noor_doc* doc) { delete doc; }

NOOR_API const char* noor_doc_project_info(noor_doc* doc) {
    if (!doc) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&] { return give(doc->doc->project_info()); });
}

NOOR_API const char* noor_doc_assets(noor_doc* doc, int32_t rescan) {
    if (!doc) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* {
        auto a = doc->doc->assets(rescan != 0);
        if (!a) {
            fail_with(a.error());
            return nullptr;
        }
        return give(*a);
    });
}

NOOR_API const char* noor_doc_scenes(noor_doc* doc) {
    if (!doc) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&] { return give(doc->doc->scenes()); });
}

NOOR_API int32_t noor_doc_open_scene(noor_doc* doc, const char* relative_path) {
    if (!doc || !relative_path) return 0;
    return guarded<>(int32_t{0}, [&]() -> int32_t {
        auto s = doc->doc->open_scene(relative_path);
        return s ? 1 : fail_with(s.error());
    });
}

NOOR_API int32_t noor_doc_new_scene(noor_doc* doc, const char* relative_path, const char* name) {
    if (!doc || !relative_path) return 0;
    return guarded<>(int32_t{0}, [&]() -> int32_t {
        auto s = doc->doc->new_scene(relative_path, name ? name : "Untitled");
        return s ? 1 : fail_with(s.error());
    });
}

NOOR_API int32_t noor_doc_save_scene(noor_doc* doc) {
    if (!doc) return 0;
    return guarded<>(int32_t{0}, [&]() -> int32_t {
        auto s = doc->doc->save_scene();
        return s ? 1 : fail_with(s.error());
    });
}

NOOR_API const char* noor_doc_scene(noor_doc* doc) {
    if (!doc) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&] { return give(doc->doc->scene_snapshot()); });
}

NOOR_API int32_t noor_doc_execute(noor_doc* doc, const char* command_json) {
    if (!doc || !command_json) return 0;
    return guarded<>(int32_t{0}, [&]() -> int32_t {
        auto cmd = noor::asset::parse_json(command_json, "command");
        if (!cmd) return fail_with(cmd.error());
        auto s = doc->doc->execute(*cmd);
        return s ? 1 : fail_with(s.error());
    });
}

NOOR_API int32_t noor_doc_undo(noor_doc* doc) {
    if (!doc) return 0;
    return guarded<>(int32_t{0}, [&]() -> int32_t { return doc->doc->undo() ? 1 : 0; });
}

NOOR_API int32_t noor_doc_redo(noor_doc* doc) {
    if (!doc) return 0;
    return guarded<>(int32_t{0}, [&]() -> int32_t { return doc->doc->redo() ? 1 : 0; });
}

NOOR_API uint64_t noor_doc_revision(noor_doc* doc) { return doc ? doc->doc->revision() : 0; }

NOOR_API int32_t noor_doc_play(noor_doc* doc) {
    if (!doc) return 0;
    return guarded<>(int32_t{0}, [&]() -> int32_t {
        auto s = doc->doc->play();
        return s ? 1 : fail_with(s.error());
    });
}

NOOR_API void noor_doc_stop(noor_doc* doc) {
    if (!doc) return;
    guarded<>(0, [&] {
        doc->doc->stop();
        return 0;
    });
}

NOOR_API void noor_doc_tick(noor_doc* doc, double seconds, noor_viewport* input_viewport) {
    if (!doc) return;
    guarded<>(0, [&] {
        doc->doc->tick(seconds, noor_viewport_game_input(input_viewport));
        if (const auto want = doc->doc->take_mouse_capture_request(); want && input_viewport)
            noor_viewport_set_mouse_capture(input_viewport, *want ? 1 : 0);
        return 0;
    });
}

NOOR_API const void* noor_rt_get_api(void) { return noor::scripting::rt_api(); }

NOOR_API void noor_doc_set_script_host(noor_doc* doc, const void* host) {
    if (doc) doc->doc->set_script_host(host);
}

NOOR_API void noor_doc_set_audio_device(noor_doc* doc, int32_t enabled) {
    if (doc) doc->doc->set_audio_device(enabled != 0);
}

NOOR_API const char* noor_doc_poll_files(noor_doc* doc) {
    if (!doc) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&] { return give(doc->doc->poll_files()); });
}

} // extern "C"

// For viewport.cpp: the channel a document publishes to.
std::shared_ptr<noor::editor::SnapshotChannel> noor_doc_channel(noor_doc* doc) {
    return doc ? doc->doc->channel() : nullptr;
}
