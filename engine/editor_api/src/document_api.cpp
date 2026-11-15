// C ABI for documents (ADR-025). Thin wrappers: parse/serialize JSON, turn errors into
// noor_last_error(), never let a C++ exception cross the DLL boundary.

#include "noor_editor_api.h"

#include "api_internal.h"
#include "document.h"

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

} // extern "C"

// For viewport.cpp: the channel a document publishes to.
std::shared_ptr<noor::editor::SnapshotChannel> noor_doc_channel(noor_doc* doc) {
    return doc ? doc->doc->channel() : nullptr;
}
