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

#define NOOR_EDITOR_API_VERSION 0u

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

#ifdef __cplusplus
}
#endif

#endif /* NOOR_EDITOR_API_H */
