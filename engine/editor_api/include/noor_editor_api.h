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
 * 4: play mode, asset files watched for changes (live reload).
 * 5: play mode runs the runtime World (ADR-027); noor_doc_tick takes the game-view viewport.
 * 6: "create" command; physics (ADR-010) and Lua (ADR-009) run in play mode; primitive shapes.
 * 7: character controller, game camera, mouse capture; C# script host (noor_rt_api / noor_rt_host).
 * 8: audio (ADR-011) in play mode; noor_doc_set_audio_device; noor_rt_api v2 (sound).
 * 9: noor_viewport_set_play (play mode ticked on the viewport's render thread, in step with the
 *    screen); "mouse_capture" viewport events. */
#define NOOR_EDITOR_API_VERSION 27u

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
/* v12: the renderer's numbers for the last frame, as JSON (refreshed about twice a second):
   {"gpu_ms", "passes": [{"name", "ms"}], "draw_calls", "triangles", "shadow_draw_calls",
    "shadow_draws_culled", "transparent_draws", "msaa_samples", "texture_bytes", "mesh_bytes",
    "render_target_bytes", "width", "height", "fps"}. The string is valid until the next call on
   this thread. */
NOOR_API const char* noor_viewport_render_stats(noor_viewport* viewport);
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
/* v22 (Milestone 7, part 5): everything about one asset: {"guid", "path", "importer", "name", "bytes",
   "interchange": null | the Noor Interchange manifest ({"id", "name", "kind", "generator", "recipe": {"file",
   "seed", "sha256"}, "created", "bounds", "lods", "collision", "family", "wind", "tags", "license", "validation",
   "provenance", "tool_blocks", "issues": [{"field", "message", "error"}], "levels": [{"level", "file", "exists",
   "bytes", "triangles", "manifest_triangles", "max_distance", "billboard", "materials", "images",
   "collision_nodes", "parts", "issues"}], "level_bytes"}), "model": {"triangles", "materials", "images",
   "nodes"} for a model without a manifest}. Cached until its files change. NULL if there's no such asset. */
NOOR_API const char* noor_doc_asset_details(noor_doc* doc, const char* asset_id);
/* v23 (Milestone 7, part 7): hand-editing a scatter component's instances: {"scatter": entity id, "phase":
   "begin" | "dab" | "end", "tool": "plant" | "remove", "radius" (m), "asset" (plant: the model; default: the
   set's first), "dabs": [{"point": [x, y, z]}] (world)}. Plant adds one tree per `radius` along a drag;
   remove takes out every instance within `radius` (generated ones by id into "removed", hand-placed ones
   from "added"). A stroke is one undo step. Returns {"planted", "removed"} or NULL. */
NOOR_API const char* noor_doc_scatter_edit(noor_doc* doc, const char* command);
/* v10: a material from the texture set `texture_path` (project-relative) belongs to, written beside
   it as <set>.noormat (kept if it exists). Returns {"path", "created", "maps": [{"role", "path"}]} or NULL. */
NOOR_API const char* noor_doc_make_material(noor_doc* doc, const char* texture_path);
/* v11: the material preview: {"shape": "sphere" | "cube" | "plane", "material": "Assets/x.noormat",
   "hdri": "Assets/sky.hdr" or "" (procedural sky), "ev100": 14, "intensity": 8000, "rotation": 0,
   "reload": n (a new number reloads materials and textures at once)}. Returns 1 or 0. */
NOOR_API int32_t noor_doc_set_preview(noor_doc* doc, const char* json);
/* v24: environment assets (Milestone 8A, part 6). noor_environment_presets: the built-in presets,
   [{"name", "description", "environment"}] (never written to). noor_doc_environment runs one command:
     {"op": "list"}                               -> [{"path", "guid", "name", "preset", "environment"}]
     {"op": "read", "asset": "Assets/Environments/x.noorenv"}  -> that object
     {"op": "apply", "preset": "Golden Evening"} or {"op": "apply", "asset": path}: a preset is first copied into
        Assets/Environments (kept, with its edits, once there); the scene's environment component becomes
        {"asset": id} (one undo step; a new "Environment" entity if the scene has none)  -> the asset's object
     {"op": "duplicate", "asset": path, "name"?}  -> the new asset's object (same settings, same preset)
     {"op": "reset", "asset": path}               -> back to the built-in preset it came from
     {"op": "save", "asset": path, "environment": {...}, "name"?}  -> its object
   NULL on error (noor_last_error). */
NOOR_API const char* noor_environment_presets(void);
NOOR_API const char* noor_doc_environment(noor_doc* doc, const char* command);
/* v25: effect assets (Milestone 8B, part 4). noor_effect_presets: the built-in effect presets,
   [{"name", "description", "emitter"}] (never written to). noor_doc_effect runs one command:
     {"op": "list"}                                   -> [{"path", "guid", "name", "preset", "emitter"}]
     {"op": "read", "asset": "Assets/Effects/x.noorfx"} -> that object
     {"op": "apply", "preset": "Fire" | "asset": path, "entity"?: id, "position"?: [x, y, z]}: a preset is first copied
        into Assets/Effects (kept, with its edits, once there); that entity's emitter becomes {"asset": id}, or a new
        entity named after the effect gets one (one undo step)  -> the asset's object plus "entity"
     {"op": "duplicate", "asset": path, "name"?}      -> the new asset's object
     {"op": "reset", "asset": path}                   -> back to the built-in preset it came from
     {"op": "save", "asset": path, "emitter": {...}, "name"?}  -> its object (the viewports' preview follows at once)
     {"op": "preview", "solo"?: id | null, "muted"?: [ids], "restart"?: true, "paused"?: bool}
        -> the preview's state {"solo", "muted", "restart", "paused"}: how viewports preview effects while editing
   NULL on error (noor_last_error). */
NOOR_API const char* noor_effect_presets(void);
NOOR_API const char* noor_doc_effect(noor_doc* doc, const char* command);
/* v26: sprite sheets (Milestone 8B, part 5). noor_doc_sprite runs one command:
     {"op": "list"}                                    -> [sheet objects]
     {"op": "read", "asset": "Assets/Sprites/x.noorsprite"} -> {"path", "guid", "name", "sheet" (the file), "texture_path",
        "image": {"width", "height"}, "frames": [[x, y, w, h], ...] (pixels, top-left origin), "columns", "rows",
        "frame_size_m": [w, h]}; "error" in place of the frames when the grid doesn't fit the image
     {"op": "create", "texture": path, "name"?, "grid"?} -> a new sheet beside the texture (one frame until a grid is set)
     {"op": "save", "asset": path, "sheet": {...}}     -> its object (a sheet that doesn't parse is refused)
     {"op": "apply", "asset": path, "entity"?: id, "animation"?, "position"?: [x, y, z]}: that entity's sprite shows this
        sheet (its other sprite fields kept), or a new entity gets one (one undo step) -> the sheet's object plus "entity"
   NULL on error (noor_last_error). */
NOOR_API const char* noor_doc_sprite(noor_doc* doc, const char* command);
/* v27: the Animation panel (Milestone 8C, part 6). noor_doc_animation runs one command:
     {"op": "model", "asset": "Assets/x.glb"} -> {"path", "guid", "name", "clips": [{"name", "duration", "channels", "nodes"}],
        "nodes": [{"name", "parent", "joint"}], "skinned", "bone_map": {"preset", "usable", "bones": [{"bone" ("LeftHand"),
        "required", "node" (a node's name or null), "corrected" (set in the .noormeta)}], "warnings": [...]}}
     {"op": "set_bone", "asset": path, "bone": "LeftHand", "node": name | null} corrects the map in the model's .noormeta
        ("auto": true forgets the correction; "bone": "*" with "auto" forgets them all) -> the model's object
     {"op": "controller", "asset": "Assets/x.nooranim"} -> {"path", "guid", "name", "controller" (the file), "error"?}
     {"op": "save_controller", "asset": path, "controller": {...}} -> its object (one that doesn't parse is refused)
     {"op": "characters"} -> the open scene's animated or skinned models: [{"entity", "name", "model", "model_guid",
        "animator" (the component or null), "controller", "retarget_from" (paths or null), "clips", "skinned"}]
     {"op": "preview", "entity"?: id | null, "clip"?, "controller"?: bool, "time"?: s, "parameters"?: {...}, "layer"?,
        "state"?: name | null, "transition"?: index | null, "skeleton"?: bool, "in_place"?: bool, "highlight"?: node | null}:
        one character posed in the viewports (the scene isn't changed) by a clip at a time, or by its animator's
        controller: running from its start with these parameters (stepped at 60 a second), one state from its start, or
        one transition (its "from" state for its exit time or half a time through, then the fade to "to"). Keys given
        change the preview, the rest stay; "entity": null ends it. -> {"entity", "mode", "time", "length", "fingerprint"
        (of the pose), "nodes", "joints", "bones", "root_position"?, "highlight_position"?, "states", "fade_weights",
        "part" ("before" | "fading" | "after": a transition), "error"? and the settings}
   NULL on error (noor_last_error). */
NOOR_API const char* noor_doc_animation(noor_doc* doc, const char* command);
/* v13: a texture's import settings and what they make of it (from the image header):
   {"settings": {"kind", "normal_convention", "mips", "max_size", "compression"},
    "source_width", "source_height", "width", "height", "mips", "format" ("BC7 sRGB", "BC5", "RGBA8"...),
    "gpu_bytes", "rgba8_bytes"}. NULL on error. */
NOOR_API const char* noor_doc_texture(noor_doc* doc, const char* texture_path);
/* v15: viewport "click" events on a CSG model also carry "face" (the brush's logical face), and
   noor_viewport_set_face_highlight outlines one brush face (NULL brush: none). */
NOOR_API void noor_viewport_set_face_highlight(noor_viewport* viewport, const char* brush_id, int32_t face);
/* v14: the open scene's CSG models (Milestone 6): [{"model": id, "brushes", "triangles", "volume"
   (cubic metres), "build_ms", "warnings": [...], "faces": [{"brush", "face", "material" (project path
   or null), "triangles"}]} (v15)]. Brushes are `brush` components on the children
   of an entity with a `csg_model` component, combined in child order. The command {"op": "move",
   "entity": id, "before": sibling id | null} reorders siblings (null: to the end). */
NOOR_API const char* noor_doc_csg(noor_doc* doc);
/* v16: exports a CSG model (entity id) of the open scene as a Noor Interchange asset: `glb_path` and a
   `.noor.json` manifest beside it (textures embedded, a collision node if the model has a collider).
   Returns {"path", "manifest", "warnings"}, or NULL (noor_last_error). */
NOOR_API const char* noor_doc_export_csg(noor_doc* doc, const char* model_id, const char* glb_path);
/* v17: sculpting a terrain (Milestone 6, part 6). command: {"terrain": entity id, "phase": "begin" |
   "dab" | "end", "tool": "raise" | "lower" | "smooth" | "flatten", "radius" (m), "strength" (m/s;
   smooth: fraction per second), "falloff" (0 hard .. 1 soft), "dabs": [{"point": [x, y, z] world,
   "seconds"}]}. Brush settings are read at "begin". A stroke is one undo step; the first stroke on a
   terrain makes its sculpt layer (Assets/Terrain/<name>.noorsculpt, the component's "sculpt"), which
   is written when the scene is saved. Returns {"sculpt": asset id, "changed": bool} or NULL.
   noor_viewport_set_brush shows the brush circle and turns left-drags on the terrain into "stroke"
   events: {"type": "stroke", "phase": "begin" | "dab" | "end", "terrain": id, "point": [x, y, z],
   "seconds"} (no "point" when the pointer is off the terrain). */
/* v18: painting (Milestone 6, part 7) through the same call: "tool": "paint" with "material": a
   .noormat asset id. The terrain's "layers" (up to four {"material", "tiling"}) gain the material if
   it isn't one of them yet; the first paint stroke makes Assets/Terrain/<name>.noorpaint (the
   component's "paint"). Strength: how far towards the material per second (1 = all the way).
   Returns {"paint": asset id, "layer": index, "changed"} for paint strokes. Fast pointer moves are
   filled in with dabs along the way (sculpting too). */
NOOR_API const char* noor_doc_sculpt(noor_doc* doc, const char* command_json);
/* v19: the ground height (world y) of a terrain entity at world (x, z), including sculpting that
   isn't saved yet (for placing things on the ground). Returns 1, or 0 (noor_last_error). */
NOOR_API int32_t noor_doc_terrain_height(noor_doc* doc, const char* terrain_id, float x, float z, float* out_height);
/* v20: runs a procedural graph (Milestone 7; `graph_path` project-relative, a .noorgraph) on a background
   thread, one at a time; out_dir NULL: <project>/Generated/<graph name>. The same code as
   `noor_cli generate`, so the same bytes. Results are cached in <project>/.noor/cache/procgraph.
   Returns 1, or 0 (noor_last_error: the file can't be read, or a graph is already running).
   Status (poll it): {"running", "graph", "done", "total", "node"} while running; when finished also
   {"ok", "cancelled", "fingerprint" (hex), "ran", "cached", "files" (project-relative), "error",
   "issues": [{"node", "pin", "message"}]}. Cancel: running nodes stop, nothing is written. Closing the
   document cancels and waits. */
NOOR_API int32_t noor_doc_generate_start(noor_doc* doc, const char* graph_path, const char* out_dir);
NOOR_API const char* noor_doc_generate_status(noor_doc* doc);
NOOR_API void noor_doc_generate_cancel(noor_doc* doc);
/* v21 (the graph editor, Milestone 7 part 4): the status of a finished run also has "nodes": {id: {"state":
   "done" | "failed" | "skipped", "message", "ms", "cached", "tiles_run", "tiles_cached", "tiles_passed",
   "outputs": {pin: {"type", "unit", "value" (numbers), "min", "max", "samples" (fields)}}}}, "written" (files
   were written: only when every declared output was made) and the run's tile counts. A graph with no
   outputs runs too (for its previews). noor_doc_generate_preview: one node output of the last run as
   {"type", "unit", "width", "height", "min", "max", "pixels": base64 of 8-bit greyscale (black = min, white
   = max; masks 0..1), rows from -z to +z} or {"value"} for numbers; `size` is the longer side. */
NOOR_API const char* noor_doc_generate_preview(noor_doc* doc, const char* node, const char* pin, int32_t size);
/* v21: every node type the engine has: [{"name", "version", "help", "tiled", "inputs"/"outputs": [{"name",
   "type", "unit", "optional"}], "params": [{"name", "kind" (number, integer, choice, text, flag), "default",
   "min", "max", "choices", "help"}]}]. */
NOOR_API const char* noor_graph_node_types(void);
/* v21: checks a graph (its JSON) without running it: {"issues": [{"node", "pin", "message"}], "text": the graph
   in the engine's own file format (save that, so files are the same whoever writes them)}. NULL if it can't be
   read at all (noor_last_error). */
NOOR_API const char* noor_graph_check(const char* graph_json);
/* {"terrain": entity id | null (off), "radius": metres}. While set, the gizmo and click-to-select
   are off in the viewport. */
NOOR_API void noor_viewport_set_brush(noor_viewport* viewport, const char* brush_json);
/* v13: new import settings for a texture (the "settings" object above; missing keys get defaults).
   Written to its .noormeta; viewports reload the texture. Returns 1 or 0. */
NOOR_API int32_t noor_doc_set_texture_settings(noor_doc* doc, const char* texture_path, const char* settings_json);
NOOR_API const char* noor_doc_scenes(noor_doc* doc);                 /* ["Scenes/Main.noorscene", ...] */

NOOR_API int32_t noor_doc_open_scene(noor_doc* doc, const char* relative_path);
NOOR_API int32_t noor_doc_new_scene(noor_doc* doc, const char* relative_path, const char* name);
NOOR_API int32_t noor_doc_save_scene(noor_doc* doc);
/* {open, path, name, modified, can_undo, undo_label, can_redo, redo_label, revision,
 *  entities: [{id, name, parent, layer, tags, transform, components, from_prefab, instance?}], issues} */
NOOR_API const char* noor_doc_scene(noor_doc* doc);

/* Command JSON: {"op": "set_transform" | "rename" | "set_component" | "remove" | "reparent" |
 * "add_asset" | "create" | "begin" | "end", ...}. See ADR-025.
 * create: {"op": "create", "name", "id"?, "parent"?, "transform"?, "components"?}, one undo step. */
NOOR_API int32_t noor_doc_execute(noor_doc* doc, const char* command_json);
NOOR_API int32_t noor_doc_undo(noor_doc* doc);
NOOR_API int32_t noor_doc_redo(noor_doc* doc);
NOOR_API uint64_t noor_doc_revision(noor_doc* doc);

/* Play mode (ADR-026): runs a copy of the scene; Stop discards it. While playing,
 * noor_doc_scene shows the running copy ("playing": true) and edits/undo are refused.
 * The host calls noor_doc_tick with the elapsed seconds (e.g. every UI frame) and the viewport
 * that acts as the game view: its keyboard and mouse become the game's input while it has the
 * keyboard focus (NULL: no input). Actions and axes come from the project's Settings/input.json. */
NOOR_API int32_t noor_doc_play(noor_doc* doc);
NOOR_API void noor_doc_stop(noor_doc* doc);
NOOR_API void noor_doc_tick(noor_doc* doc, double seconds, noor_viewport* input_viewport);

/* Checks the project's asset files for changes on disk (call every ~0.5 s). Changed models are
 * reloaded by viewports and prefabs re-read. Returns a JSON array of the changed relative paths. */
NOOR_API const char* noor_doc_poll_files(noor_doc* doc);

/* C# scripts (ADR-008). The engine functions scripts call (a const noor_rt_api*, see
 * noor_rt_api.h), and the managed host that runs them (a const noor_rt_host*, which must stay
 * valid while the document plays; NULL: C# scripts are skipped with a message). */
NOOR_API const void* noor_rt_get_api(void);
NOOR_API void noor_doc_set_script_host(noor_doc* doc, const void* host);

/* Sound in play mode goes to the speakers (enabled = 1, default) or is mixed silently (0, for
 * automated tests). Takes effect at the next Play. */
NOOR_API void noor_doc_set_audio_device(noor_doc* doc, int32_t enabled);

/* Viewports showing documents. Passing NULL detaches. The viewport keeps working if the
 * document is closed first (it keeps showing the last published scene). */
NOOR_API void noor_viewport_set_document(noor_viewport* viewport, noor_doc* doc);
/* v11: shows the document's material preview (noor_doc_set_preview) instead of its scene. */
NOOR_API void noor_viewport_set_preview(noor_viewport* viewport, noor_doc* doc);
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
 *    "fly_speed": 8.0, "look_sensitivity": 1.0, "gizmo_scale": 1.0, "msaa": 4}
 * msaa (v12): scene anti-aliasing samples, 1 (off), 2, 4 or 8 (lowered to what the GPU supports).
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

/* Play mode in step with the screen: while `doc` is set, the viewport's render thread runs one
 * play-mode frame of it (noor_doc_tick with this viewport's game input) before each frame it draws.
 * A script's mouse capture request arrives as a {"type": "mouse_capture", "capture": bool} event
 * (noor_viewport_poll_events) for the UI thread to apply with noor_viewport_set_mouse_capture.
 * NULL stops; it returns once no tick is running, so noor_doc_stop can follow safely. */
NOOR_API void noor_viewport_set_play(noor_viewport* viewport, noor_doc* doc);

/* For automated tests: while enabled, the viewport ignores the real mouse and uses this pointer
 * (physical pixels; buttons: 1 left, 2 right, 4 middle, 8 Ctrl, 16 Alt), and counts as having the
 * keyboard focus (keys posted to its window are game input even if the app isn't in front).
 * Pass enabled = 0 to give the mouse back. */
/* Mouse capture for first-person look (UI thread): the cursor is hidden and kept in the viewport and
 * raw movement becomes the game's look input. Capturing only works while the viewport has the
 * keyboard focus; Escape, focus loss and capture = 0 release it. Play mode applies scripts'
 * capture requests (noor.input.capture_mouse) through this. */
NOOR_API void noor_viewport_set_mouse_capture(noor_viewport* viewport, int32_t capture);
NOOR_API int32_t noor_viewport_mouse_captured(noor_viewport* viewport);
/* Whether the viewport has the keyboard focus (UI thread). Hosts use this to tell game keys from
 * editor shortcuts; it also answers yes while a test pointer is active. */
NOOR_API int32_t noor_viewport_has_focus(noor_viewport* viewport);

NOOR_API void noor_viewport_test_pointer(noor_viewport* viewport, int32_t enabled, int32_t x, int32_t y, int32_t buttons);

#ifdef __cplusplus
}
#endif

#endif /* NOOR_EDITOR_API_H */
