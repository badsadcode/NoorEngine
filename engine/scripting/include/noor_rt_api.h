/*
 * NoorEngine runtime scripting ABI (ADR-008, ADR-027) between the engine and managed (C#) scripts.
 *
 * Two tables of plain C function pointers, so it works the same whichever binary hosts the engine
 * (noor_editor_api.dll in the editor, the player executable later):
 *   - noor_rt_api:  engine functions that scripts call (the same set Lua has: docs/scripting).
 *   - noor_rt_host: functions of the managed script host (NoorEngine.Scripting) that the engine
 *                   calls to create script instances and run their hooks.
 *
 * Rules: UTF-8 strings. Strings returned by noor_rt_api stay valid until the next call on the
 * same thread. noor_rt_api functions may only be called while the engine is running a script
 * hook (they act on the world that called the hook). Entity ids are the 16 GUID bytes, as two
 * little-endian 64-bit halves. Calls that name a missing entity do nothing / return 0.
 */
#ifndef NOOR_RT_API_H
#define NOOR_RT_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NOOR_RT_API_VERSION 17u /* 2: sound; 3: HUD; 4: sprites, emitter bursts; 5: cross-fades, animation parameters; 6: bones, animation events; 7: water; 8: characters in water; 9: rivers' flow; 10: jump release, drop (push kinds 3, 4); 11: shape casts, overlaps, character contacts; 12: tiles; 13: spawning, teleport, tags; 14: pause, time scale, real time; 15: messages, calls; 16: tweens, last_error; 17: timelines */

typedef struct noor_rt_entity { uint64_t lo, hi; } noor_rt_entity;
typedef struct noor_rt_vec3 { float x, y, z; } noor_rt_vec3;
typedef struct noor_rt_hit {
    int32_t hit;  /* 0: nothing was hit */
    noor_rt_entity entity;
    noor_rt_vec3 point, normal;
    float distance;
} noor_rt_hit;

/* get_vec / set_vec channels */
enum {
    NOOR_RT_POSITION = 0,       /* local; get/set */
    NOOR_RT_EULER = 1,          /* local, degrees (pitch, yaw, roll); get/set */
    NOOR_RT_SCALE = 2,          /* local; get/set */
    NOOR_RT_WORLD_POSITION = 3, /* get */
    NOOR_RT_FORWARD = 4,        /* world -Z; get */
    NOOR_RT_RIGHT = 5,          /* get */
    NOOR_RT_UP = 6,             /* get */
    NOOR_RT_VELOCITY = 7        /* physics body; get/set */
};
/* push kinds */
enum { NOOR_RT_IMPULSE = 0, NOOR_RT_FORCE = 1, NOOR_RT_CHARACTER_MOVE = 2, NOOR_RT_CHARACTER_JUMP_RELEASE = 3 /* v10 */, NOOR_RT_CHARACTER_DROP = 4 /* v10 */ };
/* sound(): ops */
enum { NOOR_RT_SOUND_PLAY = 0, NOOR_RT_SOUND_STOP = 1, NOOR_RT_SOUND_IS_PLAYING = 2 };
/* input queries */
enum { NOOR_RT_DOWN = 0, NOOR_RT_PRESSED = 1, NOOR_RT_RELEASED = 2 };
/* hooks (noor_rt_host.call) */
enum { NOOR_RT_START = 0, NOOR_RT_UPDATE = 1, NOOR_RT_FIXED_UPDATE = 2, NOOR_RT_LATE_UPDATE = 3, NOOR_RT_DESTROY = 4,
       NOOR_RT_JOBS = 5 /* v14: run the script's due timers and waits; every script's, before any UPDATE */ };
/* events (noor_rt_host.event), same order as runtime::Event::Kind. ANIMATION_END, ANIMATION_EVENT (v6): `other` is the
 * entity itself and noor_rt_api.event_name() gives the animation, or the event's name. */
enum { NOOR_RT_COLLISION_ENTER = 0, NOOR_RT_COLLISION_EXIT = 1, NOOR_RT_TRIGGER_ENTER = 2, NOOR_RT_TRIGGER_EXIT = 3, NOOR_RT_ANIMATION_END = 4,
       NOOR_RT_ANIMATION_EVENT = 5 };
/* sprite(): ops (an animator's clips too, since 8C part 2: the name stayed for the ABI) */
enum { NOOR_RT_SPRITE_PLAY = 0, NOOR_RT_SPRITE_STOP = 1, NOOR_RT_SPRITE_SPEED = 2, NOOR_RT_SPRITE_CROSS_FADE = 3 /* v5: speed = seconds */ };
/* clock(), set_clock() (v14) */
enum { NOOR_RT_CLOCK_GAME = 0, NOOR_RT_CLOCK_REAL = 1, NOOR_RT_CLOCK_SCALE = 2, NOOR_RT_CLOCK_PAUSED = 3 };
/* timeline() (v17) */
enum { NOOR_RT_TIMELINE_PLAY = 0, NOOR_RT_TIMELINE_STOP = 1, NOOR_RT_TIMELINE_CURRENT = 2 };
/* tween_op() (v16) */
enum { NOOR_RT_TWEEN_CANCEL = 0, NOOR_RT_TWEEN_ACTIVE = 1 };
/* tag(): ops (v13) */
enum { NOOR_RT_TAG_HAS = 0, NOOR_RT_TAG_ADD = 1, NOOR_RT_TAG_REMOVE = 2 };
/* animation_parameter(): kinds */
enum { NOOR_RT_PARAM_NUMBER = 0, NOOR_RT_PARAM_BOOL = 1 };

typedef struct noor_rt_api {
    uint32_t version; /* NOOR_RT_API_VERSION */
    uint32_t size;    /* sizeof(noor_rt_api) */

    void (*log)(int32_t level, const char* message); /* 2 info, 3 warn, 4 error */
    double (*time)(void);
    int32_t (*find)(const char* name, noor_rt_entity* out);

    int32_t (*exists)(noor_rt_entity e);
    const char* (*name)(noor_rt_entity e); /* NULL if missing */
    int32_t (*parent)(noor_rt_entity e, noor_rt_entity* out);
    int32_t (*child)(noor_rt_entity e, const char* name, noor_rt_entity* out);
    void (*destroy)(noor_rt_entity e);

    int32_t (*get_vec)(noor_rt_entity e, int32_t channel, noor_rt_vec3* out);
    void (*set_vec)(noor_rt_entity e, int32_t channel, noor_rt_vec3 v);
    void (*translate)(noor_rt_entity e, noor_rt_vec3 delta, int32_t own_axes);
    void (*rotate)(noor_rt_entity e, noor_rt_vec3 axis, float degrees);

    const char* (*get_component)(noor_rt_entity e, const char* type);        /* JSON or NULL */
    void (*set_component)(noor_rt_entity e, const char* type, const char* json); /* NULL removes */

    int32_t (*input)(int32_t query, const char* action);
    float (*axis)(const char* name);
    void (*capture_mouse)(int32_t capture);
    int32_t (*mouse_captured)(void);

    void (*raycast)(noor_rt_vec3 origin, noor_rt_vec3 direction, float max_distance, const noor_rt_entity* ignore, noor_rt_hit* out);
    void (*push)(noor_rt_entity e, int32_t kind, noor_rt_vec3 v);
    void (*character_jump)(noor_rt_entity e, float speed);
    int32_t (*is_grounded)(noor_rt_entity e);

    /* v2: sound */
    int32_t (*sound)(noor_rt_entity e, int32_t op);
    int32_t (*play_one_shot)(const char* clip, const noor_rt_vec3* position, float volume, const char* bus);
    void (*set_bus_volume)(const char* bus, float volume);

    /* v3: HUD (UI units: the screen is 720 tall). color "#RRGGBB[AA]", anchor "top_left".."bottom_right"
     * (NULL: white, top_left). 0 if the color or anchor is not understood (nothing is drawn). */
    int32_t (*ui_text)(const char* text, float x, float y, float size, const char* color, const char* anchor);
    int32_t (*ui_rect)(float x, float y, float w, float h, const char* color, const char* anchor);

    /* v4: sprites and effects (Milestone 8B, part 6). sprite(): PLAY `animation` from its start at `speed` (0 if the
     * entity has no sprite), STOP (freeze on the current frame), SPEED (from here on; 0 freezes). */
    int32_t (*sprite)(noor_rt_entity e, int32_t op, const char* animation, float speed);
    const char* (*sprite_animation)(noor_rt_entity e); /* NULL: none */
    int32_t (*burst)(noor_rt_entity e, int32_t count);   /* particles from its emitter at the next fixed step */
    const char* (*event_name)(void);                     /* during an ANIMATION_END event: the animation */

    /* v5: skeletal animation (Milestone 8C, part 3): an animator's controller parameter (a number, or a bool: 0/1).
     * 0 if the entity has no animator. */
    int32_t (*animation_parameter)(noor_rt_entity e, const char* name, float value, int32_t kind);

    /* v6 (8C part 4): the first entity of that name anywhere below e (a model's bone). */
    int32_t (*bone)(noor_rt_entity e, const char* name, noor_rt_entity* out);

    /* v7 (8D part 2): the water surface at world (x, z) now: its height and normal. 0 where there's no water. */
    int32_t (*water)(float x, float z, float* height, noor_rt_vec3* normal);

    /* v8 (8D part 5): a character in the water: 0 dry, 1 wading, 2 swimming, 3 under (its head under the surface), -1 not
     * a character; the water's depth at its feet and at the top of its head (positive: under the surface). The WATER
     * event (kind 6) comes when the state changes; event_name() gives the new state ("dry", "wading", ...). */
    int32_t (*character_water)(noor_rt_entity e, float* feet_depth, float* head_depth);

    /* v9 (8D part 6): the water's own velocity at world (x, z) now (m/s: a river's current; still water 0). 0 where
     * there's no water. */
    int32_t (*water_flow)(float x, float z, noor_rt_vec3* flow);

    /* v11 (2D part 2b): a shape (collider JSON: box, sphere, capsule, rectangle, circle) swept from `from` to `to`: the
     * first hit, like raycast. */
    void (*shape_cast)(const char* shape_json, noor_rt_vec3 from, noor_rt_vec3 to, const noor_rt_entity* ignore, noor_rt_hit* out);
    /* The entities overlapping the shape placed at `at`: up to `max` written to `out`; returns how many there are. */
    int32_t (*overlap)(const char* shape_json, noor_rt_vec3 at, const noor_rt_entity* ignore, noor_rt_entity* out, int32_t max);
    /* What a character touched in its last step: bit 0 floor, 1 wall, 2 ceiling; the floor's normal. -1: not a
     * character. (Its velocity is the VELOCITY channel, as for bodies.) */
    int32_t (*character_contacts)(noor_rt_entity e, noor_rt_vec3* floor_normal);

    /* v12 (2D part 4): a tilemap's cell (x, y), (0, 0) its bottom-left: the tile there (-1 empty), or -2 if the entity has
     * no tilemap; and setting one (-1 empties it): 1 if it was inside the map. */
    int32_t (*tile)(noor_rt_entity e, int32_t x, int32_t y);
    int32_t (*set_tile)(noor_rt_entity e, int32_t x, int32_t y, int32_t tile);

    /* v13 (gameplay foundations, part 1): a new instance of a prefab (its asset id or path) at `position`, turned by
     * `euler` (degrees), under `parent` if given: 1 and the instance's root in `out`, or 0 if the prefab isn't found.
     * Its entities start next frame. */
    int32_t (*spawn)(const char* prefab, noor_rt_vec3 position, noor_rt_vec3 euler, const noor_rt_entity* parent, noor_rt_entity* out);
    /* Moves e at once (a body or character through physics, velocity and jumps reset); `euler` NULL keeps its rotation. */
    int32_t (*teleport)(noor_rt_entity e, noor_rt_vec3 position, const noor_rt_vec3* euler);
    /* Groups: TAG_HAS (1 if e has it), TAG_ADD, TAG_REMOVE. */
    int32_t (*tag)(noor_rt_entity e, int32_t op, const char* tag);
    /* The entities with a tag, in scene order: up to `max` written to `out`; returns how many there are. */
    int32_t (*find_all)(const char* tag, noor_rt_entity* out, int32_t max);

    /* v14 (gameplay foundations, part 2): the clocks. clock(): CLOCK_GAME (seconds of game time, = time()), CLOCK_REAL
     * (seconds really passed, paused or not), CLOCK_SCALE (the time scale), CLOCK_PAUSED (1 or 0). set_clock(): SCALE,
     * PAUSED. Timers and waits are the managed side's (NoorScript.After, Every, Wait), built on these. */
    double (*clock)(int32_t which);
    void (*set_clock)(int32_t which, double value);

    /* v15 (gameplay foundations, part 3): messages. emit(): from `emitter` (NULL: game-wide) with JSON data (NULL: null);
     * returns how many listeners heard it. listen(): `owner`'s script listens for `name` from `emitter` (NULL: game-wide);
     * the engine later calls noor_rt_host.deliver(owner's instance, managed_handle, ...). Returns an id for unlisten. */
    int32_t (*emit)(const noor_rt_entity* emitter, const char* name, const char* data_json);
    int64_t (*listen)(const noor_rt_entity* emitter, const char* name, noor_rt_entity owner, int64_t managed_handle);
    void (*unlisten)(int64_t id);
    /* Calls a function of e's script (Lua or C#) with a JSON array of arguments: its result as JSON (valid until the next
     * call), or NULL with *found = 0 if there's no such function. */
    const char* (*call)(noor_rt_entity e, const char* function, const char* args_json, int32_t* found);

    /* v16 (gameplay foundations, part 4): a tween of e's property to `to_json` over `seconds` with an easing;
     * options_json {"delay", "loops", "yoyo", "real"} (NULL: none). Its id, or 0 (last_error() says why). */
    int64_t (*tween)(noor_rt_entity e, const char* property, const char* to_json, double seconds, const char* ease, const char* options_json);
    /* TWEEN_CANCEL; TWEEN_ACTIVE: 1 while it runs. */
    int32_t (*tween_op)(int64_t id, int32_t op);
    /* Why the last call that can fail (tween) failed. */
    const char* (*last_error)(void);

    /* v17 (gameplay foundations, part 5): an animation_player's timelines. TIMELINE_PLAY `name` at `speed` (returns
     * `name`, or NULL if there's no such timeline), TIMELINE_STOP, TIMELINE_CURRENT (what it plays, or NULL). */
    const char* (*timeline)(noor_rt_entity e, int32_t op, const char* name, float speed);
} noor_rt_api;

typedef struct noor_rt_host {
    /* New instance of `class_name` for entity `e` with the component's params (JSON).
     * Returns an instance handle, or 0 if it failed (see last_error). */
    int64_t (*create)(noor_rt_entity e, const char* class_name, const char* params_json);
    /* Runs a hook. Returns 0 if the script threw (it is then stopped; see last_error). */
    int32_t (*call)(int64_t instance, int32_t hook, float dt);
    int32_t (*event)(int64_t instance, int32_t kind, noor_rt_entity other);
    void (*release)(int64_t instance);
    const char* (*last_error)(void);
    /* v15: a message for one of the instance's listeners (`handle`, as given to listen()); 0 if the handler threw. */
    int32_t (*deliver)(int64_t instance, int64_t handle, const noor_rt_entity* sender, const char* name, const char* data_json);
    /* v15: calls a method of the instance by name with a JSON array of arguments. *found: 0 no such method, 1 called
     * (returns its result as JSON, NULL for none), 2 it threw (see last_error). */
    const char* (*invoke)(int64_t instance, const char* function, const char* args_json, int32_t* found);
} noor_rt_host;

#ifdef __cplusplus
}
#endif

#endif /* NOOR_RT_API_H */
