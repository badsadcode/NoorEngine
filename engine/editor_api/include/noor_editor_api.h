/*
 * NoorEngine editor API — C ABI used by NoorEditor (C#) to host engine viewports.
 *
 * STATUS: v0, spike-grade. Created for the ADR-003 Avalonia spike. The real, versioned
 * engine/editor/plugin ABI is decided in ADR-007; expect this header to change.
 *
 * Rules (preview of ADR-007):
 *  - Plain C types only. No C++ exceptions or STL across this boundary.
 *  - Strings are UTF-8, owned by the callee, valid until the next call on the same thread.
 *  - Functions that create objects return NULL on failure; noor_last_error() explains why.
 */
#ifndef NOOR_EDITOR_API_H
#define NOOR_EDITOR_API_H

#include <stdint.h>

#if defined(_WIN32)
#  if defined(NOOR_EDITOR_API_BUILD)
#    define NOOR_API __declspec(dllexport)
#  else
#    define NOOR_API __declspec(dllimport)
#  endif
#else
#  define NOOR_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 1: documents (ADR-025), viewports render documents. */
#define NOOR_EDITOR_API_VERSION 1u

NOOR_API uint32_t noor_editor_api_version(void);

/* Last error message for the calling thread ("" if none). */
NOOR_API const char* noor_last_error(void);

/* ---- Logging ------------------------------------------------------------
 * Engine log records are forwarded to the callback from ANY engine thread.
 * level: 0 trace, 1 debug, 2 info, 3 warn, 4 error, 5 fatal.
 * Pass NULL to stop forwarding. The callback must not call back into the API. */
typedef void (*noor_log_callback)(int32_t level, const char* category, const char* message, void* user);
NOOR_API void noor_set_log_callback(noor_log_callback callback, void* user);

/* ---- Viewports ------------------------------------------------------------
 * A viewport is a native child window inside `parent_window` (HWND on Windows)
 * plus a render thread drawing into it with Vulkan.
 * Create and destroy on the UI thread that owns the parent window. */
typedef struct noor_viewport noor_viewport;

typedef struct noor_viewport_desc {
    int32_t enable_validation; /* nonzero: Vulkan validation layers if installed */
} noor_viewport_desc;

typedef enum noor_viewport_state {
    NOOR_VIEWPORT_STARTING = 0,
    NOOR_VIEWPORT_RUNNING = 1,
    NOOR_VIEWPORT_FAILED = 2,
    NOOR_VIEWPORT_STOPPED = 3
} noor_viewport_state;

typedef struct noor_viewport_stats {
    int32_t state;                  /* noor_viewport_state */
    uint64_t frames_presented;
    double frames_per_second;       /* measured over the last second */
    double seconds_to_first_frame;  /* from create() to the first presented frame; 0 if none yet */
    uint32_t width, height;         /* current swapchain size in pixels */
    uint64_t swapchain_rebuilds;
    uint64_t validation_errors;
    char gpu_name[128];
} noor_viewport_stats;

NOOR_API noor_viewport* noor_viewport_create(void* parent_window, const noor_viewport_desc* desc);
NOOR_API void* noor_viewport_native_handle(noor_viewport* viewport);
NOOR_API void noor_viewport_get_stats(noor_viewport* viewport, noor_viewport_stats* out_stats);
/* Stops the render thread, releases GPU objects and destroys the child window. */
NOOR_API void noor_viewport_destroy(noor_viewport* viewport);

/* ---- Documents (ADR-025) ----------------------------------------------------
 * A document is an open project with at most one open scene, its undo history and
 * asset registry. The engine owns all of it; the editor reads JSON snapshots and
 * changes things only through noor_doc_execute(), so every edit is undoable.
 * Functions returning int return 1 on success and 0 on failure (see noor_last_error).
 * Returned strings are UTF-8 JSON owned by the DLL, valid until the next call on the
 * same thread. Use a document from one thread (the UI thread). */
typedef struct noor_doc noor_doc;

NOOR_API noor_doc* noor_doc_open_project(const char* project_path);
NOOR_API noor_doc* noor_doc_create_project(const char* folder, const char* name);
NOOR_API void noor_doc_close(noor_doc* doc);

NOOR_API const char* noor_doc_project_info(noor_doc* doc);           /* {id, name, root, engine} */
NOOR_API const char* noor_doc_assets(noor_doc* doc, int32_t rescan); /* [{guid, path, importer, name}] or NULL */
NOOR_API const char* noor_doc_scenes(noor_doc* doc);                 /* ["Scenes/Main.noorscene", ...] */

NOOR_API int32_t noor_doc_open_scene(noor_doc* doc, const char* relative_path);
NOOR_API int32_t noor_doc_new_scene(noor_doc* doc, const char* relative_path, const char* name);
NOOR_API int32_t noor_doc_save_scene(noor_doc* doc);
/* {open, path, name, modified, can_undo, undo_label, can_redo, redo_label, revision,
 *  entities: [{id, name, parent, layer, tags, transform, components, from_prefab, instance?}], issues} */
NOOR_API const char* noor_doc_scene(noor_doc* doc);

/* Command JSON: {"op": "set_transform" | "rename" | "set_component" | "remove" | "reparent" |
 * "add_asset" | "begin" | "end", ...}. See ADR-025. */
NOOR_API int32_t noor_doc_execute(noor_doc* doc, const char* command_json);
NOOR_API int32_t noor_doc_undo(noor_doc* doc);
NOOR_API int32_t noor_doc_redo(noor_doc* doc);
NOOR_API uint64_t noor_doc_revision(noor_doc* doc);

/* Viewports showing documents. Passing NULL detaches. The viewport keeps working if the
 * document is closed first (it keeps showing the last published scene). */
NOOR_API void noor_viewport_set_document(noor_viewport* viewport, noor_doc* doc);
/* Moves the viewport camera to show everything (NULL or "[]") or the given entities
 * (JSON array of ids; include descendants to frame a whole subtree). */
NOOR_API void noor_viewport_frame(noor_viewport* viewport, const char* entity_ids_json);

#ifdef __cplusplus
}
#endif

#endif /* NOOR_EDITOR_API_H */
