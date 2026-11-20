/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CC2X_NAVIGATION_H
#define CC2X_NAVIGATION_H
#include "cc2x.h"
#define CC2X_NAVIGATION_VERSION 1u
#define CC2X_NAVIGATION_QUERY "cc2x_query_navigation_v1"
#define CC2X_NAVIGATION_ID UINT64_C(0x434332584e415631)
enum {CC2X_NAVIGATION_SURFACE=1};
enum {CC2X_NAVIGATION_CONFIGURED=1,CC2X_NAVIGATION_ACTIVE=2,
      CC2X_NAVIGATION_READY=4,CC2X_NAVIGATION_BLOCKED=8,
      CC2X_NAVIGATION_ARRIVED=16,CC2X_NAVIGATION_FAULTED=32};
typedef struct cc2x_navigation_options {
 uint32_t size,flags;
 double minimum_depth,arrival_radius;
} cc2x_navigation_options;
typedef struct cc2x_navigation_state {
 uint32_t size,id,flags,waypoint_id;
 double target_x,target_z,distance,minimum_depth,arrival_radius;
} cc2x_navigation_state;
typedef struct cc2x_navigation_api {
 uint32_t version,size;uint64_t interface_id;
 int (*available)(void);
 int (*read)(uint32_t id,cc2x_navigation_state* out);
 /* Configure before acquiring/renewing this owner's motion lease. SURFACE
  * requires wet native mesh triangles; minimum_depth is 0..1000 m and radius
  * is 1..150 m. flags=0 releases the policy. Session, manual takeover, motion
  * release/expiry and owner unload also clear it. Never changes motion API 1. */
 int (*configure)(const char* owner,uint64_t generation,uint32_t id,
                  const cc2x_navigation_options* options);
} cc2x_navigation_api;
typedef const cc2x_navigation_api* (*cc2x_query_navigation_fn)(uint32_t,uint32_t);
static inline const cc2x_navigation_api* cc2x_navigation_resolve(cc2x_query_navigation_fn query){
 const cc2x_navigation_api* a=query?query(CC2X_NAVIGATION_VERSION,sizeof(cc2x_navigation_api)):NULL;
 return a&&a->version==CC2X_NAVIGATION_VERSION&&a->size>=sizeof(*a)&&
  a->interface_id==CC2X_NAVIGATION_ID&&a->available&&a->read&&a->configure?a:NULL;
}
#ifdef _WIN32
#include <windows.h>
static inline const cc2x_navigation_api* cc2x_navigation_from_core(const cc2x_api* core){
 HMODULE module=NULL;
 if(!core||core->size<offsetof(cc2x_api,server_post)+sizeof(core->server_post)||!core->server_post||
 !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
 (LPCSTR)(uintptr_t)core->server_post,&module))return NULL;
 return cc2x_navigation_resolve((cc2x_query_navigation_fn)(uintptr_t)GetProcAddress(module,CC2X_NAVIGATION_QUERY));
}
#endif
#endif
