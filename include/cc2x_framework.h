/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Shared C / Lua framework operations. Optional named extension; the stable
 * cc2x_api prefix is never reinterpreted as a fork-specific larger struct. */
#ifndef CC2X_FRAMEWORK_H
#define CC2X_FRAMEWORK_H
#include "cc2x.h"
#include <stddef.h>
#include <stdint.h>

#define CC2X_FRAMEWORK_QUERY "cc2x_query_framework_v1"
#define CC2X_FRAMEWORK_VERSION 1u
#define CC2X_FRAMEWORK_ID UINT64_C(0x4343325846574b31)
#define CC2X_FRAMEWORK_DEFAULT INT32_MIN

enum {
    CC2X_FRAMEWORK_WORLD = 1u << 0,
    CC2X_FRAMEWORK_BUTTONS = 1u << 1,
    CC2X_FRAMEWORK_THROTTLE = 1u << 2,
    CC2X_FRAMEWORK_MESSAGES = 1u << 3,
    CC2X_FRAMEWORK_LUA_BINDINGS = 1u << 4,
    CC2X_FRAMEWORK_VEHICLE_CATALOG = 1u << 5,
    CC2X_FRAMEWORK_ENTITY_METADATA = 1u << 6,
    CC2X_FRAMEWORK_SESSION_TASKS = 1u << 7
};

typedef struct cc2x_framework_event {
    uint32_t size;
    const char *name;       /* borrowed for this callback only */
    uint64_t generation;
    uint64_t tick;
    double a, b, c;         /* same copied arguments as cc2.events */
} cc2x_framework_event;
/* Return CC2X_FRAMEWORK_DEFAULT to retain the engine decision; otherwise the
 * event's documented slot/index/boolean. Never retain event pointers. */
typedef int32_t (*cc2x_framework_event_fn)(const cc2x_framework_event *event, void *user);

typedef struct cc2x_button_info {
    uint32_t index;
    char name[128];
    uint32_t on;
    uint32_t enabled;
} cc2x_button_info;
typedef struct cc2x_button_value {
    const char *name;
    uint32_t on;            /* exactly 0 or 1 */
} cc2x_button_value;
typedef struct cc2x_message {
    char topic[65];
    uint32_t subject;
    double value;
} cc2x_message;

/* Independent Host VM, not a game-owned cc2x_lua state. The stack API accepts
 * this handle only during the current registered native callback. */
typedef struct cc2x_vm cc2x_vm;
typedef int (*cc2x_vm_fn)(cc2x_vm *vm, void *user);
typedef struct cc2x_vm_api {
    uint32_t size;
    int (*gettop)(cc2x_vm *vm);
    int (*type)(cc2x_vm *vm, int index);
    int (*tointeger)(cc2x_vm *vm, int index, int64_t *out);
    int (*tonumber)(cc2x_vm *vm, int index, double *out);
    int (*toboolean)(cc2x_vm *vm, int index);
    const char *(*tostring)(cc2x_vm *vm, int index, size_t *length);
    void (*pushnil)(cc2x_vm *vm);
    void (*pushboolean)(cc2x_vm *vm, int value);
    void (*pushinteger)(cc2x_vm *vm, int64_t value);
    void (*pushnumber)(cc2x_vm *vm, double value);
    void (*pushstring)(cc2x_vm *vm, const char *value);
    void (*newtable)(cc2x_vm *vm);
    void (*setfield)(cc2x_vm *vm, const char *key);
    void (*appendi)(cc2x_vm *vm, int64_t index);
} cc2x_vm_api;

typedef struct cc2x_vehicle_declaration {
    uint32_t size;
    const char *id;
    const char *name;
    const char *base;
    const char *definition;
    const char *ui;
    uint32_t order;         /* UINT32_MAX = default identity ordering */
} cc2x_vehicle_declaration;
typedef struct cc2x_vehicle_definition {
    uint32_t definition_id, base_type, flags;
    char identity[130];
    char name[513];       /* UTF-8; supports the same 128 UTF-16 units as Lua */
} cc2x_vehicle_definition;

/* Declarative user-logic metadata only, not an engine object factory. */
typedef struct cc2x_entity_metadata {
    char owner[97], id[97], base[97], definition[97];
} cc2x_entity_metadata;
typedef struct cc2x_hangar_slot {
    uint32_t index, compatible, occupied;
} cc2x_hangar_slot;
typedef struct cc2x_hangar_result {
    uint32_t request_id, success, vehicle_id, error_code;
} cc2x_hangar_result;
typedef struct cc2x_ui_declaration {
    uint32_t size;
    const char *id;
    const char *small;
    const char *large;
    const char *loadout;
    const char *engine_type;
    uint32_t weapon;
} cc2x_ui_declaration;

typedef struct cc2x_framework_api {
    uint32_t version, size;
    uint64_t interface_id;
    uint64_t (*capabilities)(void);
    uint64_t (*subscribe)(const char *event, cc2x_framework_event_fn callback, void *user);
    int (*unsubscribe)(uint64_t subscription);
    int (*key_down)(uint32_t virtual_key);
    int (*throttle_get)(uint32_t vehicle_id, double *out);
    int (*throttle_set)(uint32_t vehicle_id, double value);
    int (*button_count)(uint32_t vehicle_id);
    int (*button_get)(uint32_t vehicle_id, uint32_t index, cc2x_button_info *out);
    int (*buttons_set)(uint32_t vehicle_id, const cc2x_button_value *values, uint32_t count);
    uint64_t (*mailbox_open)(const char *owner);
    int (*mailbox_close)(uint64_t mailbox);
    int (*mailbox_poll)(uint64_t mailbox, cc2x_message *out, uint32_t capacity);
    int (*mailbox_publish)(uint64_t mailbox, const char *topic, uint32_t subject, double value);
    int (*mailbox_forget)(uint64_t mailbox, uint32_t subject);
    int (*message_send)(const char *owner, const char *topic, uint32_t subject, double value);
    int (*message_read)(const char *owner, const char *topic, uint32_t subject, double *out);
    uint64_t (*vm_bind)(const char *owner, const char *name, cc2x_vm_fn callback, void *user);
    int (*vm_unbind)(uint64_t binding);
    const cc2x_vm_api *vm;
    /* Declarations only during plugin_load, before the shared catalog freezes.
     * Assets stay inside the selected plugin's package content root. */
    int (*declare_vehicle)(const char *owner, const cc2x_vehicle_declaration *declaration);
    int (*declare_ui)(const char *owner, const cc2x_ui_declaration *declaration);
    uint32_t (*vehicle_definition_count)(void);
    int (*vehicle_definition_get)(uint32_t index, cc2x_vehicle_definition *out);
    int (*vehicle_spawn)(const char *identity, uint32_t carrier_id, uint32_t slot);
    /* Server context only; native owners use the reserved native. prefix.
     * The same registry is visible through cc2.entities in Lua. */
    int (*entity_register)(const char *owner, const char *id, const char *base, const char *definition);
    int (*entity_remove)(const char *owner, const char *id);
    int (*entity_clear)(const char *owner);
    int (*entity_count)(void);
    int (*entity_get)(uint32_t index, cc2x_entity_metadata *out);
    /* Returns required count, -1 on failure. Copies only if capacity suffices.
     * out == NULL, capacity == 0 is a count query. */
    int (*hangar_slots)(uint32_t carrier_id, uint32_t chassis_type, cc2x_hangar_slot *out, uint32_t capacity);
    int (*hangar_result)(uint32_t request_id, cc2x_hangar_result *out);
    uint64_t (*session_generation)(void); /* zero without a session */
    uint64_t (*session_tick)(void);
    /* Next tick, bounded queue, discarded on originating session end.
     * Callback gets a copied event named "task"; return value is ignored.
     * user remains caller-owned; retain IDs, not engine pointers.
     * owner is an explicit native. namespace; 64 tasks per owner, 256 total. */
    uint64_t (*post)(const char *owner, uint64_t generation, cc2x_framework_event_fn callback, void *user);
    int (*cancel)(const char *owner, uint64_t task);
} cc2x_framework_api;

typedef const cc2x_framework_api *(*cc2x_query_framework_fn)(uint32_t version, uint32_t size);
static inline int cc2x_framework_compatible(const cc2x_framework_api *api) {
    return api && api->version == CC2X_FRAMEWORK_VERSION && api->size >= sizeof(*api)
        && api->interface_id == CC2X_FRAMEWORK_ID && api->capabilities
        && api->subscribe && api->unsubscribe;
}
static inline const cc2x_framework_api *cc2x_framework_resolve(cc2x_query_framework_fn query) {
    if (!query) return NULL;
    const cc2x_framework_api *api = query(CC2X_FRAMEWORK_VERSION, sizeof(cc2x_framework_api));
    return cc2x_framework_compatible(api) ? api : NULL;
}
#if defined(_WIN32)
#include <windows.h>
/* Resolve the extension from the already loaded core that supplied the API.
 * Does not load another DLL or guess a filename. NULL on older runtimes. */
static inline const cc2x_framework_api *cc2x_framework_from_core(const cc2x_api *core) {
    HMODULE module = NULL;
    if (!core || core->size < offsetof(cc2x_api, server_post) + sizeof(core->server_post)
        || !core->server_post || !GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)(uintptr_t)core->server_post, &module)) return NULL;
    return cc2x_framework_resolve((cc2x_query_framework_fn)(uintptr_t)GetProcAddress(module, CC2X_FRAMEWORK_QUERY));
}
#endif
#endif
