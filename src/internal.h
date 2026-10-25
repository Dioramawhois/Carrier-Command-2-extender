/* SPDX-License-Identifier: GPL-3.0-or-later */
/* CC2X internals shared by the core (cc2x.c) and the world API (world.c). Not for plugins. */
#ifndef CC2X_INTERNAL_H
#define CC2X_INTERNAL_H

#include "cc2x.h"

/* cc2x.c */
uintptr_t va(uint32_t image_va);
void api_log(const char *who, int level, const char *fmt, ...);
void *api_server(void);
int patch_call(uint32_t site, uint32_t len, const uint8_t *expected, void *fn, void **original);
int patch_mid(uint32_t site, uint32_t len, const uint8_t *expected, cc2x_mid_fn fn, void *user);
/* patches of the SDK itself, made while a plugin may be collecting its own: add() queues them,
 * they are committed all-or-nothing and the plugin's pending patches are kept */
int core_patch(int (*add)(void));
/* identity of a game Lua state (its global state), for per-state bookkeeping */
uintptr_t lua_state_key(cc2x_lua *L);

/* world.c */
int world_init(const char *build);         /* 0: build without world layout (API 3 stays empty) */
void world_fill(cc2x_api *api);
void world_session(void);                  /* a session started or ended: drop caches */
void world_tick(void);                     /* a new server tick */

/* lua_world.c: the world for Lua mods, and the API 1 snapshots */
void lua_world_init(const cc2x_api *api);
int snap_vehicles(cc2x_vehicle_info *out, int max);
int snap_vehicle(uint32_t id, cc2x_vehicle_info *out);
int snap_tiles(cc2x_tile_info *out, int max);
int snap_tile(uint32_t id, cc2x_tile_info *out);

#endif
