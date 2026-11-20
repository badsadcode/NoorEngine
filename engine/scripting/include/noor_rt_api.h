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

#define NOOR_RT_API_VERSION 7u /* 2: sound; 3: HUD; 4: sprites, emitter bursts; 5: cross-fades, animation parameters; 6: bones, animation events; 7: water */

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
enum { NOOR_RT_IMPULSE = 0, NOOR_RT_FORCE = 1, NOOR_RT_CHARACTER_MOVE = 2 };
/* sound(): ops */
enum { NOOR_RT_SOUND_PLAY = 0, NOOR_RT_SOUND_STOP = 1, NOOR_RT_SOUND_IS_PLAYING = 2 };
/* input queries */
enum { NOOR_RT_DOWN = 0, NOOR_RT_PRESSED = 1, NOOR_RT_RELEASED = 2 };
/* hooks (noor_rt_host.call) */
enum { NOOR_RT_START = 0, NOOR_RT_UPDATE = 1, NOOR_RT_FIXED_UPDATE = 2, NOOR_RT_LATE_UPDATE = 3, NOOR_RT_DESTROY = 4 };
/* events (noor_rt_host.event), same order as runtime::Event::Kind. ANIMATION_END, ANIMATION_EVENT (v6): `other` is the
 * entity itself and noor_rt_api.event_name() gives the animation, or the event's name. */
enum { NOOR_RT_COLLISION_ENTER = 0, NOOR_RT_COLLISION_EXIT = 1, NOOR_RT_TRIGGER_ENTER = 2, NOOR_RT_TRIGGER_EXIT = 3, NOOR_RT_ANIMATION_END = 4,
       NOOR_RT_ANIMATION_EVENT = 5 };
/* sprite(): ops (an animator's clips too, since 8C part 2: the name stayed for the ABI) */
enum { NOOR_RT_SPRITE_PLAY = 0, NOOR_RT_SPRITE_STOP = 1, NOOR_RT_SPRITE_SPEED = 2, NOOR_RT_SPRITE_CROSS_FADE = 3 /* v5: speed = seconds */ };
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
} noor_rt_host;

#ifdef __cplusplus
}
#endif

#endif /* NOOR_RT_API_H */
