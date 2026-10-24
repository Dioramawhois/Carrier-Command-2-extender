/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * CC2X - Carrier Command 2 extender. Public plugin API.
 *
 * A plugin is a 32-bit DLL named cc2x_<name>.dll placed next to carrier_command.exe
 * (or in <game>\cc2x\plugins\). It exports:
 *
 *     __declspec(dllexport) int cc2x_plugin_load(const cc2x_api *api);
 *
 * called once while the game starts (inside the loader lock: do not create threads, wait on
 * anything or load other DLLs there). Return 1 on success, 0 to stay inactive.
 *
 * All addresses taken by the API are *image* virtual addresses of carrier_command.exe for the
 * default base 0x00400000 (the ones Ghidra/IDA show). The SDK rebases them (ASLR).
 *
 * Threads: on_server_* callbacks run on the game's logic server thread. Lua functions and
 * on_lua_state run on the thread that runs the Lua screens. Share data between them with
 * your own lock (or use api->server_post / the snapshot functions of the SDK).
 */
#ifndef CC2X_H
#define CC2X_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CC2X_API_VERSION 2

#ifdef _WIN32
#define CC2X_EXPORT __declspec(dllexport)
#else
#define CC2X_EXPORT
#endif

/* ---------------------------------------------------------------------------------------------
 * Mid-function hook context: the registers at the hook site (pushad order) + flags.
 * Change any register (except esp) and it is restored when the hook returns.
 * Set jump_to (a rebased address, see api->va) to continue there instead of running the
 * displaced instructions and returning to the site.
 */
typedef struct cc2x_cpu {
    uint32_t edi, esi, ebp, esp_unused, ebx, edx, ecx, eax;
    uint32_t eflags;
    uint32_t jump_to;
} cc2x_cpu;

/* Stack of the hooked code at the hook site: CC2X_STACK(cpu)[0] is [esp], [1] is [esp+4] ... */
#define CC2X_STACK(cpu) ((uint32_t *)((uint8_t *)(cpu) + sizeof(cc2x_cpu)))

typedef void (*cc2x_mid_fn)(cc2x_cpu *cpu, void *user);

/* ---------------------------------------------------------------------------------------------
 * Lua. cc2x_lua is the game's lua_State (Lua 5.3). Functions you register are ordinary Lua C
 * functions: read arguments with lua_* helpers, push results, return their number.
 * Never raise Lua errors from C - return nil, "reason" instead.
 */
typedef struct cc2x_lua cc2x_lua;
typedef int (*cc2x_lua_fn)(cc2x_lua *L);

enum {
    CC2X_LUA_NONE = -1, CC2X_LUA_NIL = 0, CC2X_LUA_BOOLEAN = 1, CC2X_LUA_LIGHTUSERDATA = 2,
    CC2X_LUA_NUMBER = 3, CC2X_LUA_STRING = 4, CC2X_LUA_TABLE = 5, CC2X_LUA_FUNCTION = 6,
    CC2X_LUA_USERDATA = 7, CC2X_LUA_THREAD = 8
};

enum { CC2X_LOG_DEBUG = 0, CC2X_LOG_INFO = 1, CC2X_LOG_WARN = 2, CC2X_LOG_ERROR = 3 };

/* Snapshot records (copies; see api->vehicles / api->tiles). */
typedef struct cc2x_vehicle_info {
    uint32_t id, definition, team, dock_state, attached_to, hp;
    float fuel;
    double x, y, z;
} cc2x_vehicle_info;

typedef struct cc2x_tile_info {
    uint32_t id, team;
    double x, z;
    int32_t airfields;        /* -1 = island not loaded (no controller), else count */
} cc2x_tile_info;

typedef struct cc2x_api {
    uint32_t api_version;      /* CC2X_API_VERSION of the running SDK */
    uint32_t size;             /* sizeof(cc2x_api) of the running SDK */
    const char *sdk_version;   /* "0.2.0" */
    const char *game_build;    /* "1.5.18-win32" */
    uintptr_t module_base;     /* actual base of carrier_command.exe */
    const char *game_dir;      /* folder of carrier_command.exe, no trailing slash */

    /* image VA (base 0x400000) -> runtime address */
    uintptr_t (*va)(uint32_t image_va);

    /* cc2x.log in the game folder */
    void (*log)(const char *plugin, int level, const char *fmt, ...);

    /* ---- patching: collect, then commit all-or-nothing --------------------------------------
     * Every function checks `expected` (the original bytes, len bytes) and fails on mismatch.
     * Nothing is written before patch_commit(); if any part fails, nothing stays written.
     * Two plugins can never patch overlapping bytes.
     */
    void (*patch_begin)(const char *plugin);
    /* Replace len (>=5) bytes at site with `call fn` + nops. If the original starts with a
     * call rel32, *original receives its target (call it from fn to keep vanilla behaviour). */
    int (*patch_call)(uint32_t site, uint32_t len, const uint8_t *expected, void *fn, void **original);
    /* Mid-function hook: len (>=5) displaced bytes must be position independent (or start with
     * a rel32 call/jmp, which is relocated). fn(cpu, user) runs first, then the displaced code. */
    int (*patch_mid)(uint32_t site, uint32_t len, const uint8_t *expected, cc2x_mid_fn fn, void *user);
    /* Replace a 4-byte pointer (vtable slot). expected_target is an image VA (0 = don't check). */
    int (*patch_ptr)(uint32_t slot, uint32_t expected_target, void *fn, void **original);
    int (*patch_commit)(void);
    void (*patch_abort)(void);

    /* ---- events ----------------------------------------------------------------------------- */
    void (*on_server_tick)(void (*fn)(void *server, void *user), void *user);
    void (*on_session)(void (*fn)(void *server, int started, void *user), void *user);
    void (*on_lua_state)(void (*fn)(cc2x_lua *L, void *user), void *user);
    /* current logic server object (0 outside a session). Only meaningful on the server thread. */
    void *(*server)(void);
    /* run fn on the server thread at the next tick (safe from Lua functions). 0 = queue full */
    int (*server_post)(void (*fn)(void *server, void *user), void *user);

    /* ---- Lua bridge ------------------------------------------------------------------------- */
    /* cc2x.<ns>.<name> = fn in every game Lua state (ns NULL = directly on cc2x) */
    int (*lua_add)(const char *ns, const char *name, cc2x_lua_fn fn);
    /* cc2x.has(name) returns true */
    void (*lua_feature)(const char *name);

    int (*lua_gettop)(cc2x_lua *L);
    int (*lua_type)(cc2x_lua *L, int idx);                 /* CC2X_LUA_*, 1-based args */
    int (*lua_tointeger)(cc2x_lua *L, int idx, int64_t *out);   /* 1 if number */
    int (*lua_tonumber)(cc2x_lua *L, int idx, double *out);
    int (*lua_toboolean)(cc2x_lua *L, int idx);
    const char *(*lua_tostring)(cc2x_lua *L, int idx, size_t *len);  /* NULL if not a string */
    void (*lua_pushnil)(cc2x_lua *L);
    void (*lua_pushboolean)(cc2x_lua *L, int b);
    void (*lua_pushinteger)(cc2x_lua *L, int64_t v);
    void (*lua_pushnumber)(cc2x_lua *L, double v);
    void (*lua_pushstring)(cc2x_lua *L, const char *s);
    void (*lua_newtable)(cc2x_lua *L);
    /* t[k] = top; pops the value. The table must be just below the value. */
    void (*lua_setfield)(cc2x_lua *L, const char *k);
    /* t[i] = top for a *new* key (append to a table you built); pops the value */
    void (*lua_appendi)(cc2x_lua *L, int64_t i);

    /* ---- world snapshots (any thread; refreshed every few server ticks) ---------------------- */
    /* copy up to max records, returns the total count (0 outside a session) */
    int (*vehicles)(cc2x_vehicle_info *out, int max);
    int (*vehicle)(uint32_t id, cc2x_vehicle_info *out);   /* 1 if found */
    int (*tiles)(cc2x_tile_info *out, int max);
    int (*tile)(uint32_t id, cc2x_tile_info *out);

    /* ---- since API 2 (CC2X 0.3): plugins ship inside their mod ----------------------------------
     * A mod that is enabled in the game's mod menu (a local mod or a Steam Workshop item) has its
     * plugins loaded from <mod folder>\cc2x\*.dll. Paths are in the system ANSI code page. */
    /* folder of the plugin while cc2x_plugin_load runs (copy it; empty afterwards) */
    const char *(*plugin_dir)(void);
    /* %APPDATA%\Carrier Command 2\cc2x: settings that survive mod updates (Steam replaces the mod
     * folder on every update) */
    const char *config_dir;
} cc2x_api;

typedef int (*cc2x_plugin_load_fn)(const cc2x_api *api);

#ifdef __cplusplus
}
#endif
#endif
