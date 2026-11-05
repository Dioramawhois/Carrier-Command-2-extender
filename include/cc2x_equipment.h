/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CC2X_EQUIPMENT_H
#define CC2X_EQUIPMENT_H
#include "cc2x.h"
#define CC2X_EQUIPMENT_QUERY "cc2x_query_equipment_v1"
#define CC2X_EQUIPMENT_VERSION 1u
#define CC2X_EQUIPMENT_ID UINT64_C(0x4343325845515031)
/* Server-thread only. Docked, unattached, stationary vehicles without orders.
 * Compatible stock definitions only; -1 clears. Free: no inventory/refunds.
 * fill=1 fills the newly created attachment, not the vehicle's other stores.
 * Mods must check their own team/airfield and gameplay authorization first. */
typedef struct cc2x_equipment_api {
 uint32_t version,size;
 uint64_t interface_id;
 int (*available)(void);
 int (*set_attachment)(uint32_t vehicle,uint32_t slot,int32_t definition,int fill);
} cc2x_equipment_api;
typedef const cc2x_equipment_api* (*cc2x_query_equipment_fn)(uint32_t,uint32_t);
static inline const cc2x_equipment_api* cc2x_equipment_resolve(cc2x_query_equipment_fn query){
 const cc2x_equipment_api* a=query?query(CC2X_EQUIPMENT_VERSION,sizeof(cc2x_equipment_api)):NULL;
 return a&&a->version==CC2X_EQUIPMENT_VERSION&&a->size>=sizeof(*a)&&a->interface_id==CC2X_EQUIPMENT_ID&&a->available&&a->set_attachment?a:NULL;
}
#endif
