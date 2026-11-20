/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CC2X_MOTION_H
#define CC2X_MOTION_H
#include "cc2x.h"
#define CC2X_MOTION_VERSION 1u
#define CC2X_MOTION_QUERY "cc2x_query_motion_v1"
#define CC2X_MOTION_ID UINT64_C(0x434332584d4f5431)
enum {CC2X_MOTION_ACTIVE=1,CC2X_MOTION_MANUAL=2,CC2X_MOTION_ENGINE=4,CC2X_MOTION_LOCAL=8,CC2X_MOTION_GUIDANCE=16};
enum {CC2X_CONTACT_VEHICLE=1,CC2X_CONTACT_PROJECTILE=2};
typedef struct cc2x_motion_state {
 uint32_t size,id,team,flags,mode,waypoints;
 uint64_t generation;
 double speed,lever,forward_power,target_x,target_z;
 double heading_error,path_distance,yaw_rate,steering;
} cc2x_motion_state;
typedef struct cc2x_contact {
 uint32_t id,kind,definition,team;
 double x,y,z;
} cc2x_contact;
typedef struct cc2x_motion_api {
 uint32_t version,size;uint64_t interface_id;
 int (*available)(void);
 int (*read)(uint32_t id,cc2x_motion_state* out);
 /* Authoritative local single-peer session only. Native path steering;
  * power limits existing forward water jets, never a body/velocity write.
  * Renew every server tick; stale leases stop after three missed ticks.
  * active=0 releases and neutralizes only the requesting owner's lease. */
 int (*set)(const char* owner,uint64_t generation,uint32_t id,int active,double power);
 int (*contacts)(uint32_t carrier,cc2x_contact* out,uint32_t capacity,uint32_t* count);
 /* Native steering with consumer-selected normalized forward command. Engine
  * output, fuel and disabled state still gate thrust. Release through set(). */
 int (*drive)(const char* owner,uint64_t generation,uint32_t id,double power_cap,double forward);
} cc2x_motion_api;
typedef const cc2x_motion_api* (*cc2x_query_motion_fn)(uint32_t,uint32_t);
static inline const cc2x_motion_api* cc2x_motion_resolve(cc2x_query_motion_fn query){
 const cc2x_motion_api* a=query?query(CC2X_MOTION_VERSION,sizeof(cc2x_motion_api)):NULL;
 return a&&a->version==CC2X_MOTION_VERSION&&a->size>=sizeof(*a)&&a->interface_id==CC2X_MOTION_ID&&a->available&&a->read&&a->set&&a->contacts&&a->drive?a:NULL;
}
#ifdef _WIN32
#include <windows.h>
static inline const cc2x_motion_api* cc2x_motion_from_core(const cc2x_api* core){
 HMODULE module=NULL;
 if(!core||core->size<offsetof(cc2x_api,server_post)+sizeof(core->server_post)||!core->server_post||
 !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
 (LPCSTR)(uintptr_t)core->server_post,&module))return NULL;
 return cc2x_motion_resolve((cc2x_query_motion_fn)(uintptr_t)GetProcAddress(module,CC2X_MOTION_QUERY));
}
#endif
#endif
