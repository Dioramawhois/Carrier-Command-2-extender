/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Optional, additive carrier operations. Stable API 8/framework 1 unchanged. */
#ifndef CC2X_CARRIER_H
#define CC2X_CARRIER_H
#include "cc2x.h"
#define CC2X_CARRIER_QUERY "cc2x_query_carrier_v1"
#define CC2X_CARRIER_VERSION 1u
#define CC2X_CARRIER_ID UINT64_C(0x4343325843415231)
enum { CC2X_CARRIER_DOCK=1, CC2X_CARRIER_RECOVERY=2 };
enum {
 CC2X_DOCK_PENDING=0, CC2X_DOCK_ACCEPTED=1, CC2X_DOCK_ALREADY_QUEUED=2,
 CC2X_DOCK_INVALID_VEHICLE=3, CC2X_DOCK_PROFILE_REFUSED=4,
 CC2X_DOCK_ENGINE_REFUSED=5, CC2X_DOCK_ENGINE_FAULT=6
};
enum {CC2X_RECOVERY_NONE=0,CC2X_RECOVERY_DISARMED=1,CC2X_RECOVERY_EXPIRED=2,
 CC2X_RECOVERY_INVALIDATED=3,CC2X_RECOVERY_TAKEN_OVER=4,CC2X_RECOVERY_ABORTED=5};
typedef struct cc2x_recovery_options {
 uint32_t size, window_ticks, grants, touch_jumps; /* 1..600, 1..3, 1..30; rotor window <=300 */
} cc2x_recovery_options;
typedef struct cc2x_recovery_status {
 uint32_t size, vehicle_id, carrier_id, armed, grants, touch_jumps, remaining_ticks, reason;
 uint64_t generation;
} cc2x_recovery_status;
typedef struct cc2x_dock_result {
 uint32_t size, carrier_id, vehicle_id, code;
 uint64_t request_id, generation;
} cc2x_dock_result;
typedef struct cc2x_carrier_api {
 uint32_t version,size;
 uint64_t interface_id;
 int (*available)(uint32_t capability); /* discovery never patches */
 /* Server thread/context only. Explicit owner starts with native.; Lua uses
  * its module identity implicitly. generation is framework.session_generation.
  * Request accepted != game queued != stowed. Poll the owner-scoped result.
  * Bounded: 16 pending, 4 per owner, 64 retained results. No network RPC. */
 uint64_t (*dock_request)(const char* owner,uint64_t generation,uint32_t carrier,uint32_t vehicle);
 int (*dock_result)(const char* owner,uint64_t request,cc2x_dock_result* out);
 int (*recovery_arm)(const char* owner,uint64_t generation,uint32_t carrier,uint32_t vehicle,const cc2x_recovery_options* options);
 int (*recovery_disarm)(const char* owner,uint32_t vehicle);
 int (*recovery_status)(const char* owner,uint32_t vehicle,cc2x_recovery_status* out);
} cc2x_carrier_api;
typedef const cc2x_carrier_api* (*cc2x_query_carrier_fn)(uint32_t,uint32_t);
static inline const cc2x_carrier_api* cc2x_carrier_resolve(cc2x_query_carrier_fn query) {
 const cc2x_carrier_api* a=query?query(CC2X_CARRIER_VERSION,sizeof(cc2x_carrier_api)):NULL;
 return a&&a->version==CC2X_CARRIER_VERSION&&a->size>=sizeof(*a)&&a->interface_id==CC2X_CARRIER_ID&&
  a->available&&a->dock_request&&a->dock_result&&a->recovery_arm&&a->recovery_disarm&&a->recovery_status?a:NULL;
}
#if defined(_WIN32)
#include <windows.h>
static inline const cc2x_carrier_api* cc2x_carrier_from_core(const cc2x_api* core) {
 HMODULE module=NULL;
 if(!core||core->size<offsetof(cc2x_api,server_post)+sizeof(core->server_post)||!core->server_post||
 !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
 (LPCSTR)(uintptr_t)core->server_post,&module))return NULL;
 return cc2x_carrier_resolve((cc2x_query_carrier_fn)(uintptr_t)GetProcAddress(module,CC2X_CARRIER_QUERY));
}
#endif
#endif
