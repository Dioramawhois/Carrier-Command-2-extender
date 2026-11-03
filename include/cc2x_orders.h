/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Read-only origin of the current incoming engine waypoint order. Not RPC. */
#ifndef CC2X_ORDERS_H
#define CC2X_ORDERS_H
#include "cc2x.h"
#define CC2X_ORDERS_QUERY "cc2x_query_orders_v1"
#define CC2X_ORDERS_VERSION 1u
#define CC2X_ORDERS_ID UINT64_C(0x434332584f524431)
#define CC2X_ORDER_BROADCAST_PENDING 1u
typedef struct cc2x_waypoint_order {
    uint32_t size;
    uint32_t vehicle_id;
    uint32_t peer_id; /* engine peer ID, including zero; never a Steam/account ID */
    uint32_t flags;
    uint64_t generation; /* local core-session epoch, NOT a network session ID */
} cc2x_waypoint_order;
typedef struct cc2x_orders_api {
    uint32_t version, size;
    uint64_t interface_id;
    int (*available)(void); /* exact supported profile and receive-path guards */
    /* Caller initializes out->size. Only during this thread's incoming waypoint
     * callback and its original core epoch. No pointer ownership is transferred.
     * Sender existence is checked, team/admin permissions are NOT established.
     * The engine broadcasts the final waypoint state after this callback. */
    int (*current)(cc2x_waypoint_order *out);
} cc2x_orders_api;
typedef const cc2x_orders_api *(*cc2x_query_orders_fn)(uint32_t version,uint32_t size);
static inline const cc2x_orders_api *cc2x_orders_resolve(cc2x_query_orders_fn query) {
    const cc2x_orders_api *api=query?query(CC2X_ORDERS_VERSION,sizeof(cc2x_orders_api)):NULL;
    return api&&api->version==CC2X_ORDERS_VERSION&&api->size>=sizeof(*api)&&
        api->interface_id==CC2X_ORDERS_ID&&api->available&&api->current?api:NULL;
}
#if defined(_WIN32)
#include <windows.h>
static inline const cc2x_orders_api *cc2x_orders_from_core(const cc2x_api *core) {
    HMODULE module=NULL;
    if(!core||core->size<offsetof(cc2x_api,server_post)+sizeof(core->server_post)||!core->server_post||
       !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCSTR)(uintptr_t)core->server_post,&module))return NULL;
    return cc2x_orders_resolve((cc2x_query_orders_fn)(uintptr_t)GetProcAddress(module,CC2X_ORDERS_QUERY));
}
#endif
#endif
