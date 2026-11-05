/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CC2X_TILE_SITES_H
#define CC2X_TILE_SITES_H
#include "cc2x.h"
#define CC2X_TILE_SITES_QUERY "cc2x_query_tile_sites_v1"
#define CC2X_TILE_SITES_VERSION 1u
#define CC2X_TILE_SITES_ID UINT64_C(0x4343325854535431)
typedef struct cc2x_tile_site { double transform[12]; uint32_t flags_low,flags_high; } cc2x_tile_site;
/* Server context, current session only. CPU layout generation is synchronous
 * and cached; callers should request at most one new tile per update. It does
 * not stream islands, spawn simulation objects, or change the world's RNG.
 * Result matrices describe placed environment objects. Stock airfields are
 * kind=0, definition=3, the same selector used by the native airfield builder. */
typedef struct cc2x_tile_sites_api {
 uint32_t version,size;uint64_t interface_id;
 int (*read)(uint32_t tile,uint32_t kind,uint32_t definition,cc2x_tile_site* out,uint32_t capacity,uint32_t* count);
} cc2x_tile_sites_api;
#endif
