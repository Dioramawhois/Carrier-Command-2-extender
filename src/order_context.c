/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "order_context.h"
#include <string.h>
#include <stdatomic.h>
static _Atomic int supported;
static _Thread_local cc2x_order_scope current_scope;
void cc2x_orders_profile(int ready) {atomic_store(&supported,ready!=0);}
int cc2x_orders_available(void) {return atomic_load(&supported);}
int cc2x_orders_current(cc2x_waypoint_order *out) {
    if(!out||out->size!=sizeof(*out)||!cc2x_orders_available()||!current_scope.active||
       !current_scope.generation||!current_scope.record.generation||
       current_scope.generation()!=current_scope.record.generation)return 0;
    *out=current_scope.record;return 1;
}
void cc2x_orders_enter(const cc2x_waypoint_order *record,uint64_t (*generation)(void),cc2x_order_scope *previous) {
    *previous=current_scope;memset(&current_scope,0,sizeof(current_scope));
    if(record&&record->size==sizeof(*record)&&record->generation&&generation) {
        current_scope.record=*record;current_scope.generation=generation;current_scope.active=1;
    }
}
void cc2x_orders_leave(const cc2x_order_scope *previous) {current_scope=*previous;}
CC2X_EXPORT const cc2x_orders_api *cc2x_query_orders_v1(uint32_t version,uint32_t size) {
    static const cc2x_orders_api api={CC2X_ORDERS_VERSION,sizeof(api),CC2X_ORDERS_ID,cc2x_orders_available,cc2x_orders_current};
    return version==CC2X_ORDERS_VERSION&&size==sizeof(api)?&api:NULL;
}
