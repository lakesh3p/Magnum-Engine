/*
 * Magnum game module API (C ABI).
 *
 * Game code lives in a DLL built from Game/. The editor loads it, calls the exports below and hands
 * it a table of host functions. There is no shared C++ between editor and game: only the plain
 * structs in this file cross the boundary, so the DLL can be rebuilt and reloaded while the editor runs.
 *
 * A game *component* is a block of plain data (POD) plus a table that names its fields. The editor uses
 * that table for the Details panel, undo, scene files and for keeping values across hot reloads
 * (values are saved by field NAME, so you can add, remove, reorder or retype fields freely).
 * Anything you want to survive a reload must therefore be listed, even if it is hidden from Details.
 *
 * Exports every game module provides:
 *     MAGNUM_EXPORT int32_t Magnum_OnLoad(const MagnumHostApi* host);   // register components; return 0 on success
 *     MAGNUM_EXPORT void    Magnum_OnUpdate(float dt);                // once per frame while playing
 *     MAGNUM_EXPORT void    Magnum_OnUnload(void);                    // before the DLL is replaced or the editor exits
 */
#ifndef MAGNUM_GAME_API_H
#define MAGNUM_GAME_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAGNUM_GAME_API_VERSION 1

typedef uint64_t MagnumActorId; /* 0 = no actor */

enum MagnumLogLevel { MAGNUM_LOG_INFO = 0, MAGNUM_LOG_WARN = 1, MAGNUM_LOG_ERROR = 2 };

enum MagnumPropKind {
    MAGNUM_PROP_BOOL = 0,   /* bool                                      */
    MAGNUM_PROP_INT = 1,    /* int32_t                                   */
    MAGNUM_PROP_FLOAT = 2,  /* float                                     */
    MAGNUM_PROP_VEC3 = 3,   /* float[3]                                  */
    MAGNUM_PROP_COLOR3 = 4, /* float[3], shown as a color picker         */
    MAGNUM_PROP_STRING = 5, /* char[capacity], always null-terminated    */
    MAGNUM_PROP_ENUM = 6    /* int32_t index into `options` ("A|B|C")    */
};

enum MagnumPropFlags {
    MAGNUM_PROP_HIDDEN = 1 /* saved and kept across reloads, but not shown in Details */
};

typedef struct MagnumPropertyDesc {
    const char* name;
    const char* category;
    int32_t kind;
    uint32_t offset;     /* byte offset inside the component struct */
    uint32_t capacity;   /* STRING: size of the char array including the terminator */
    float min_value;     /* FLOAT: slider range; min == max == 0 means "no range" */
    float max_value;
    float speed;         /* drag speed in Details, 0 = default */
    const char* options; /* ENUM: labels separated by '|' */
    uint32_t flags;
} MagnumPropertyDesc;

typedef struct MagnumTransform {
    float position[3];
    float rotation[3]; /* degrees: x = pitch, y = yaw, z = roll */
    float scale[3];
} MagnumTransform;

typedef struct MagnumComponentDesc {
    const char* name;                    /* unique, shown in Details and stored in scene files */
    uint32_t size;                       /* sizeof the struct; it must be trivially copyable */
    const MagnumPropertyDesc* properties;
    uint32_t property_count;
    void (*construct)(void* data);       /* fill in default values (data is zeroed first); may be NULL */
    void (*on_begin)(MagnumActorId actor, void* data);              /* play starts; may be NULL */
    void (*on_update)(MagnumActorId actor, void* data, float dt);   /* every frame while playing; may be NULL */
    void (*on_end)(MagnumActorId actor, void* data);                /* play stops; may be NULL */
} MagnumComponentDesc;

/* Functions the editor provides. Valid from Magnum_OnLoad until Magnum_OnUnload returns. */
typedef struct MagnumHostApi {
    uint32_t version; /* MAGNUM_GAME_API_VERSION the editor was built with */
    void (*log)(int32_t level, const char* message);
    void (*register_component)(const MagnumComponentDesc* desc); /* only during Magnum_OnLoad; the table is copied */

    /* Actors. Return 0 on failure (unknown actor, or called outside play/update callbacks). */
    int32_t (*get_transform)(MagnumActorId actor, MagnumTransform* out);
    int32_t (*set_transform)(MagnumActorId actor, const MagnumTransform* value);
    MagnumActorId (*find_actor)(const char* name);
    int32_t (*get_actor_name)(MagnumActorId actor, char* buffer, uint32_t capacity);

    /* Input and time while playing. Key codes are GLFW's (see the MAGNUM_KEY_* values below). */
    int32_t (*key_down)(int32_t key);
    double (*time)(void); /* seconds since play started */
} MagnumHostApi;

#define MAGNUM_KEY_SPACE 32
#define MAGNUM_KEY_A 65
#define MAGNUM_KEY_D 68
#define MAGNUM_KEY_E 69
#define MAGNUM_KEY_Q 81
#define MAGNUM_KEY_S 83
#define MAGNUM_KEY_W 87
#define MAGNUM_KEY_LEFT_SHIFT 340
#define MAGNUM_KEY_UP 265
#define MAGNUM_KEY_DOWN 264
#define MAGNUM_KEY_LEFT 263
#define MAGNUM_KEY_RIGHT 262

#ifdef _WIN32
#define MAGNUM_EXPORT __declspec(dllexport)
#else
#define MAGNUM_EXPORT
#endif

typedef int32_t (*MagnumOnLoadFn)(const MagnumHostApi* host);
typedef void (*MagnumOnUpdateFn)(float dt);
typedef void (*MagnumOnUnloadFn)(void);

#ifdef __cplusplus
} /* extern "C" */

/* Helpers for writing property tables in C++:
 *   static const MagnumPropertyDesc props[] = {
 *       MAGNUM_FLOAT(Spinner, speed, "Spin", 0, 360, 1.0f),
 *       MAGNUM_VEC3(Spinner, axis, "Spin"),
 *   };
 */
#define MAGNUM_PROP_(Type, member, category, kind, capacity, lo, hi, speed, options, flags) \
    { #member, category, kind, (uint32_t)offsetof(Type, member), capacity, lo, hi, speed, options, flags }
#define MAGNUM_BOOL(Type, member, category) MAGNUM_PROP_(Type, member, category, MAGNUM_PROP_BOOL, 0, 0.0f, 0.0f, 0.0f, nullptr, 0)
#define MAGNUM_INT(Type, member, category) MAGNUM_PROP_(Type, member, category, MAGNUM_PROP_INT, 0, 0.0f, 0.0f, 0.0f, nullptr, 0)
#define MAGNUM_FLOAT(Type, member, category, lo, hi, speed) \
    MAGNUM_PROP_(Type, member, category, MAGNUM_PROP_FLOAT, 0, lo, hi, speed, nullptr, 0)
#define MAGNUM_VEC3(Type, member, category) MAGNUM_PROP_(Type, member, category, MAGNUM_PROP_VEC3, 0, 0.0f, 0.0f, 0.0f, nullptr, 0)
#define MAGNUM_COLOR3(Type, member, category) MAGNUM_PROP_(Type, member, category, MAGNUM_PROP_COLOR3, 0, 0.0f, 0.0f, 0.0f, nullptr, 0)
#define MAGNUM_STRING(Type, member, category) \
    MAGNUM_PROP_(Type, member, category, MAGNUM_PROP_STRING, (uint32_t)sizeof(((Type*)0)->member), 0.0f, 0.0f, 0.0f, nullptr, 0)
#define MAGNUM_ENUM(Type, member, category, options) \
    MAGNUM_PROP_(Type, member, category, MAGNUM_PROP_ENUM, 0, 0.0f, 0.0f, 0.0f, options, 0)
/* State that must survive a hot reload but should not clutter Details. */
#define MAGNUM_HIDDEN_FLOAT(Type, member) MAGNUM_PROP_(Type, member, "", MAGNUM_PROP_FLOAT, 0, 0.0f, 0.0f, 0.0f, nullptr, MAGNUM_PROP_HIDDEN)
#endif

#endif /* MAGNUM_GAME_API_H */
