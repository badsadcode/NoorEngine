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

/* 1: documents (ADR-025), viewports render documents.
 * 2: viewport selection highlight, gizmos and events (picking, gizmo edits).
 * 3: scale gizmo, viewport options (snapping, camera speed, gizmo size).
 * 4: play mode, asset files watched for changes (live reload). */
#define NOOR_EDITOR_API_VERSION 4u

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

/* Play mode (ADR-026): runs a copy of the scene; Stop discards it. While playing,
 * noor_doc_scene shows the running copy ("playing": true) and edits/undo are refused.
 * The host calls noor_doc_tick with the elapsed seconds (e.g. every UI frame). */
NOOR_API int32_t noor_doc_play(noor_doc* doc);
NOOR_API void noor_doc_stop(noor_doc* doc);
NOOR_API void noor_doc_tick(noor_doc* doc, double seconds);

/* Checks the project's asset files for changes on disk (call every ~0.5 s). Changed models are
 * reloaded by viewports and prefabs re-read. Returns a JSON array of the changed relative paths. */
NOOR_API const char* noor_doc_poll_files(noor_doc* doc);

/* Viewports showing documents. Passing NULL detaches. The viewport keeps working if the
 * document is closed first (it keeps showing the last published scene). */
NOOR_API void noor_viewport_set_document(noor_viewport* viewport, noor_doc* doc);
/* Moves the viewport camera to show everything (NULL or "[]") or the given entities
 * (JSON array of ids; include descendants to frame a whole subtree). */
NOOR_API void noor_viewport_frame(noor_viewport* viewport, const char* entity_ids_json);

/* Entities drawn highlighted (JSON array of ids; include descendants to highlight a subtree).
 * NULL or "[]" clears. */
NOOR_API void noor_viewport_set_selection(noor_viewport* viewport, const char* entity_ids_json);

typedef enum noor_gizmo_mode {
    NOOR_GIZMO_NONE = 0,
    NOOR_GIZMO_MOVE = 1,   /* world-axis arrows + an XZ plane handle */
    NOOR_GIZMO_ROTATE = 2, /* world-axis rings, around the entity's origin */
    NOOR_GIZMO_SCALE = 3   /* entity-axis handles + a uniform centre cube */
} noor_gizmo_mode;

/* Viewport preferences as JSON; missing keys keep their value. Defaults:
 *   {"snap": false, "move_step": 1.0, "rotate_step": 15.0, "scale_step": 0.1,
 *    "fly_speed": 8.0, "look_sensitivity": 1.0, "gizmo_scale": 1.0}
 * Snapping: moves land on a world grid of move_step metres, rotations on multiples of
 * rotate_step degrees, scales on multiples of scale_step. Holding Ctrl while dragging inverts "snap". */
NOOR_API void noor_viewport_set_options(noor_viewport* viewport, const char* options_json);

/* Shows a transform gizmo on one entity (its id as a string; NULL hides it). Dragging it
 * previews the change in the viewport and, on release, reports a "transform" event: the
 * viewport never edits the document itself. */
NOOR_API void noor_viewport_set_gizmo(noor_viewport* viewport, const char* entity_id, int32_t mode);

/* Drains the viewport's events as a JSON array (UI thread; string valid until the next call):
 *   {"type":"click","entity":"<id>"|null}  left click without dragging; entity = the mesh or
 *                                           marker under the pointer (expanded entity id)
 *   {"type":"transform","entity":"<id>","label":"Move"|"Rotate",
 *    "transform":{position,rotation,scale}} a finished gizmo drag: the entity's new LOCAL transform */
NOOR_API const char* noor_viewport_poll_events(noor_viewport* viewport);

/* Projects a world point with the camera of the last drawn frame: out = {x, y, distance},
 * x/y in the viewport's physical pixels (origin top-left). Returns 0 if the point is behind
 * the camera or nothing was drawn yet. Gizmo arrows are NOOR_GIZMO_SCREEN_SCALE x their
 * distance from the camera long (times the "gizmo_scale" option). */
NOOR_API int32_t noor_viewport_world_to_screen(noor_viewport* viewport, const float world[3], float out[3]);
#define NOOR_GIZMO_SCREEN_SCALE 0.16f

#ifdef __cplusplus
}
#endif

#endif /* NOOR_EDITOR_API_H */
