/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * CC2X - Carrier Command 2 extender. Public plugin API.
 *
 * A plugin is a 32-bit DLL shipped inside its mod: <mod folder>\content\cc2x\<name>.dll, loaded
 * while the mod is enabled in the game's mod menu (for development also cc2x_<name>.dll next to
 * carrier_command.exe). It exports:
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
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CC2X_API_VERSION 3

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

/* ---------------------------------------------------------------------------------------------
 * The live game world (API 3). World axes: x and z horizontal, y up (height over the sea),
 * metres. A vehicle's nose points along its local +z, +y is up and +x is its left side.
 */
typedef struct cc2x_body {
    double pos[3];            /* world position of the main body (the vehicle position), m */
    double rot[4];            /* orientation quaternion x, y, z, w (cc2x_quat_from_ypr) */
    double vel[3];            /* m/s */
    double spin[3];           /* angular velocity, rad/s */
} cc2x_body;
enum { CC2X_BODY_POS = 1, CC2X_BODY_ROT = 2, CC2X_BODY_VEL = 4, CC2X_BODY_SPIN = 8, CC2X_BODY_ALL = 15 };

typedef struct cc2x_hit {
    double pos[3], normal[3];
    double distance;
} cc2x_hit;
enum { CC2X_RAY_STATIC = 1, CC2X_RAY_DYNAMIC = 2 };

/* Lists and fields. r = read, rw = read and write, [i] = index of an item, vec = index 0..2 is
 * x, y, z. Lists of objects give their items with ref(); lists of ids with geti(). */
enum {
    /* the world (obj NULL) */
    CC2X_WORLD_VEHICLES = 0x001,    /* list of vehicles: every unit, carriers, aircraft, barges... */
    CC2X_WORLD_TILES,               /* list of tiles: the map squares, each with one island */

    /* vehicle */
    CC2X_VEHICLE_ID = 0x100,        /* r  id, as Lua and the save files know it */
    CC2X_VEHICLE_DEFINITION,        /* r  e_game_object_type (cc2x_game.h) */
    CC2X_VEHICLE_TEAM,              /* r  team id */
    CC2X_VEHICLE_TEAM_IS_AI,        /* r  1 = an AI team owns it */
    CC2X_VEHICLE_IS_AIR,            /* r  1 = aircraft */
    CC2X_VEHICLE_IS_WING,           /* r  1 = fixed-wing aircraft */
    CC2X_VEHICLE_IS_ROTOR,          /* r  1 = helicopter */
    CC2X_VEHICLE_HP,                /* r  hit points */
    CC2X_VEHICLE_FUEL,              /* r  fuel */
    CC2X_VEHICLE_POS,               /* r  vec: world position of the main body */
    CC2X_VEHICLE_ATTACHED_TO,       /* r  id of the carrier it stands on, 0 = none */
    CC2X_VEHICLE_DOCK_STATE,        /* rw CC2X_DOCK_* */
    CC2X_VEHICLE_DOCK_VEHICLE,      /* rw id of the carrier it docks with, 0 = none */
    CC2X_VEHICLE_DOCK_TILE,         /* rw id of the tile it docks with, 0 = none */
    CC2X_VEHICLE_DOCK_AIRFIELD,     /* rw index of the airfield on that tile, -1 = none */
    CC2X_VEHICLE_FLIGHT_STATE,      /* rw CC2X_FLIGHT_* (the airfield and carrier code set it) */
    CC2X_VEHICLE_ATTACHMENTS,       /* list of attachments (count) */
    CC2X_VEHICLE_ATTACHMENT_FLYING, /* rw [i] 1 while the aircraft flies, 0 parked / on the ground */
    CC2X_VEHICLE_WHEELS,            /* list of wheels (count); an aircraft's wheel 0 is its nose wheel */
    CC2X_VEHICLE_WHEEL_CONTACT,     /* r  [i] > 0 while wheel i touches the ground */
    CC2X_VEHICLE_CONTROL_MODE,      /* rw 0 = its waypoints, 3 = fly to CONTROL_TARGET (airfield code) */
    CC2X_VEHICLE_CONTROL_TARGET,    /* rw vec: autopilot target, y = height */
    CC2X_VEHICLE_CONTROL_SPEED,     /* rw autopilot speed, m/s */
    CC2X_VEHICLE_CONTROL_POWER,     /* rw engine power 0..1 (0 = engine off, parked) */
    CC2X_VEHICLE_CONTROL_TAXI,      /* rw 1 = ground control (taxiing, parked) */
    CC2X_VEHICLE_WAYPOINTS,         /* list of waypoints (ref); list_clear removes them all */

    /* waypoint (ref of CC2X_VEHICLE_WAYPOINTS) */
    CC2X_WAYPOINT_TYPE = 0x180,     /* rw e_waypoint_type: CC2X_WAYPOINT_* */
    CC2X_WAYPOINT_PARAM,            /* rw for CC2X_WAYPOINT_BARGE_LOAD_TILE: the tile id */

    /* tile */
    CC2X_TILE_ID = 0x200,           /* r  id */
    CC2X_TILE_TEAM,                 /* r  owner team id */
    CC2X_TILE_POS,                  /* r  vec: centre */
    CC2X_TILE_ISLAND,               /* ref: its island controller, NULL while the game has none */
    CC2X_TILE_AIRFIELDS,            /* r  airfields of the island (also remembered while the island
                                       controller is unloaded), -1 = not known yet */

    /* island controller (ref CC2X_TILE_ISLAND) */
    CC2X_ISLAND_AIRFIELDS = 0x280,  /* list of airfields (ref) */

    /* airfield (ref of CC2X_ISLAND_AIRFIELDS) */
    CC2X_AIRFIELD_TILE = 0x300,     /* r  tile id */
    CC2X_AIRFIELD_TRANSFORM,        /* r  [0..11]: rows x axis, y axis, z axis, origin (world) */
    CC2X_AIRFIELD_RUNWAY_START,     /* r  vec: world position of the runway's start node */
    CC2X_AIRFIELD_RUNWAY_END,       /* r  vec: world position of its end node */
    CC2X_AIRFIELD_HANGARS,          /* list of hangar slots: rw [i] vehicle id, 0 = free */
    CC2X_AIRFIELD_STATE,            /* rw runway state of the game's airfield code (0 = idle) */
    CC2X_AIRFIELD_MODE,             /* rw its mode (0 = idle) */
    CC2X_AIRFIELD_RUNWAY_VEHICLE,   /* rw id of the aircraft on the runway, -1 = none */
    CC2X_AIRFIELD_TAXI_PATH,        /* list of taxi path nodes (count; list_clear) */
    CC2X_AIRFIELD_DOCK_QUEUE,       /* list of ids: aircraft waiting to land */
    CC2X_AIRFIELD_UNDOCK_QUEUE,     /* list of ids: aircraft waiting to take off */
};

/* e_vehicle_dock_state */
enum {
    CC2X_DOCK_UNDOCKED = 0, CC2X_DOCK_DOCKING = 1, CC2X_DOCK_PENDING_UNDOCK = 2, CC2X_DOCK_UNDOCKING = 3,
    CC2X_DOCK_DOCKED = 4, CC2X_DOCK_QUEUED = 5, CC2X_DOCK_TAXI = 6, CC2X_DOCK_UNDOCK_HOLDING = 7
};
/* CC2X_VEHICLE_FLIGHT_STATE */
enum { CC2X_FLIGHT_PARKED = 0, CC2X_FLIGHT_GROUND = 1, CC2X_FLIGHT_FLYING = 2 };
/* e_waypoint_type */
enum {
    CC2X_WAYPOINT_MOVE = 0, CC2X_WAYPOINT_DOCK = 1, CC2X_WAYPOINT_SUPPORT = 2, CC2X_WAYPOINT_DEPLOY = 3,
    CC2X_WAYPOINT_BARGE_LOAD_TILE = 4, CC2X_WAYPOINT_BARGE_UNLOAD_CARRIER = 5
};

/* orientation from heading (yaw: 0 = nose along +z, +pi/2 = along +x), pitch (nose up > 0) and
 * roll (left side up > 0) */
static inline void cc2x_quat_from_ypr(double yaw, double pitch, double roll, double q[4]) {
    double cy = cos(yaw / 2), sy = sin(yaw / 2);
    double cp = cos(-pitch / 2), sp = sin(-pitch / 2);       /* about +x: nose up is negative */
    double cr = cos(roll / 2), sr = sin(roll / 2);
    double t[4] = {cy * sp, sy * cp, -sy * sp, cy * cp};      /* yaw (about +y) * pitch */
    q[0] = t[0] * cr + t[1] * sr;                             /* * roll (about +z) */
    q[1] = t[1] * cr - t[0] * sr;
    q[2] = t[3] * sr + t[2] * cr;
    q[3] = t[3] * cr - t[2] * sr;
}
/* heading of a horizontal direction, as cc2x_quat_from_ypr takes it */
static inline double cc2x_yaw_of(double dx, double dz) { return atan2(dx, dz); }

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
     * plugins loaded from <mod folder>\content\cc2x\*.dll. Paths are in the system ANSI code page. */
    /* folder of the plugin while cc2x_plugin_load runs (copy it; empty afterwards) */
    const char *(*plugin_dir)(void);
    /* %APPDATA%\Carrier Command 2\cc2x: settings that survive mod updates (Steam replaces the mod
     * folder on every update) */
    const char *config_dir;

    /* ---- since API 3 (CC2X 0.4): the live game world --------------------------------------------
     * The game's own objects: vehicles, tiles (map squares with islands), island controllers,
     * airfields and waypoints, read and changed through named fields and lists (CC2X_* below); the
     * SDK knows where they live in each supported game build, so plugins carry no game addresses.
     * Server thread only (on_server_tick, server_post, the on_* events below). The game deletes
     * objects at any tick: keep ids, not pointers, from one tick to the next.
     * Unknown field, wrong index or no session: 0 / NULL. See docs/WORLD_API.md.
     */
    void *(*find)(int list, uint32_t id);        /* CC2X_WORLD_VEHICLES / CC2X_WORLD_TILES by id */
    void *(*tile_at)(double x, double z);        /* tile under a world position */
    int (*count)(const void *obj, int list);     /* number of items in a list (obj NULL: world) */
    void *(*ref)(const void *obj, int field, int index);    /* object in a list / object field */
    int64_t (*geti)(const void *obj, int field, int index); /* index: list item / vector component */
    double (*getf)(const void *obj, int field, int index);
    int (*seti)(void *obj, int field, int index, int64_t value);   /* 1 = written */
    int (*setf)(void *obj, int field, int index, double value);
    int (*list_remove)(void *obj, int list, int index);     /* id lists (queues) */
    int (*list_push)(void *obj, int list, int64_t value);   /* append to an id list (queue) */
    int (*list_clear)(void *obj, int list);

    /* physics of a vehicle's main body (a PhysX rigid body): world position, orientation,
     * velocity, spin. body_set writes the parts in `what` (CC2X_BODY_*), waking the body. */
    int (*body_get)(const void *vehicle, cc2x_body *out);
    int (*body_set)(void *vehicle, const cc2x_body *in, int what);
    /* the game's PhysX scene query: first hit from `from` along unit vector `dir` within max_dist
     * (CC2X_RAY_STATIC = terrain and buildings, CC2X_RAY_DYNAMIC = vehicles). 1 = hit */
    int (*raycast)(const double from[3], const double dir[3], double max_dist, int flags, cc2x_hit *hit);

    /* game functions */
    void (*vehicle_brakes)(void *vehicle, float brake, float grip);   /* 0..1 each */
    void (*vehicle_gear)(void *vehicle, int down);
    /* the game's landing request: queue an aircraft at one of the island's airfields, as for an AI
     * aircraft (the dock state becomes CC2X_DOCK_QUEUED when accepted). 1 = queued */
    int (*request_landing)(void *island, void *vehicle);
    /* send a vehicle's waypoints to the clients after changing them */
    void (*waypoints_sync)(void *vehicle);
    /* airfield coordinates (x across, y up, z along the hangar row) -> world */
    void (*airfield_to_world)(const void *airfield, const double local[3], double world[3]);
    /* parking bay of hangar slot 0..3: world position on the hangar floor, nose heading (yaw) */
    int (*airfield_bay)(const void *airfield, int slot, double pos[3], double *yaw);

    /* ---- game events that need a hook: the hook is installed by the first subscriber (call these
     * from cc2x_plugin_load). 1 = subscribed. Server thread. */
    /* a vehicle got new waypoints (a player's order; see CC2X_VEHICLE_WAYPOINTS) */
    int (*on_waypoints)(void (*fn)(void *vehicle, void *user), void *user);
    /* after the game's tick of each island airfield */
    int (*on_airfield_tick)(void (*fn)(void *airfield, void *user), void *user);
    /* an airfield picks the hangar slot for a landing aircraft: return a slot, or -1 to leave the
     * choice to the next plugin / the game */
    int (*on_hangar_choice)(int (*fn)(void *airfield, void *vehicle, void *user), void *user);
    /* the game decides whether islands of `team` get an island controller (the AI that runs
     * airfields, repairs and defence spawns). Vanilla: AI teams only. Return 1 to give one. */
    int (*on_island_controller)(int (*fn)(int team, void *user), void *user);
    /* an island controller is about to spawn the island's defence units (ai_team: the game counts
     * the owner as an AI team). Return 0 to skip the spawn. */
    int (*on_island_spawn)(int (*fn)(void *tile, int ai_team, void *user), void *user);
} cc2x_api;

typedef int (*cc2x_plugin_load_fn)(const cc2x_api *api);

#ifdef __cplusplus
}
#endif
#endif
