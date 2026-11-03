/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CC2X_ORDER_CONTEXT_H
#define CC2X_ORDER_CONTEXT_H
#include "cc2x_orders.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct cc2x_order_scope {
    cc2x_waypoint_order record;
    uint64_t (*generation)(void);
    int active;
} cc2x_order_scope;
/* Internal engine adapter only. These are not exports in the public DLL. */
void cc2x_orders_profile(int supported);
int cc2x_orders_available(void);
int cc2x_orders_current(cc2x_waypoint_order *out);
void cc2x_orders_enter(const cc2x_waypoint_order *record,uint64_t (*generation)(void),cc2x_order_scope *previous);
void cc2x_orders_leave(const cc2x_order_scope *previous);
CC2X_EXPORT const cc2x_orders_api *cc2x_query_orders_v1(uint32_t version,uint32_t size);
#ifdef __cplusplus
}
#endif
#endif
