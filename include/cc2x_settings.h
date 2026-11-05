/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CC2X_SETTINGS_H
#define CC2X_SETTINGS_H
#include "cc2x.h"
#define CC2X_SETTINGS_QUERY "cc2x_query_settings_v1"
#define CC2X_SETTINGS_VERSION 1u
#define CC2X_SETTINGS_ID UINT64_C(0x4343325853455431)
typedef struct cc2x_settings_api {
 uint32_t version,size;
 uint64_t interface_id;
 /* Server thread/context only. Explicit native.<owner>; no arbitrary paths.
  * Module files are private, typed, bounded and persisted atomically. */
 int (*number)(const char* owner,const char* key,double fallback,double minimum,double maximum,double* out);
 int (*boolean)(const char* owner,const char* key,int fallback,int* out);
 int (*set_number)(const char* owner,const char* key,double value);
 int (*set_boolean)(const char* owner,const char* key,int value);
 int (*reload)(const char* owner);
} cc2x_settings_api;
typedef const cc2x_settings_api* (*cc2x_query_settings_fn)(uint32_t,uint32_t);
static inline const cc2x_settings_api* cc2x_settings_resolve(cc2x_query_settings_fn query){
 const cc2x_settings_api* a=query?query(CC2X_SETTINGS_VERSION,sizeof(cc2x_settings_api)):NULL;
 return a&&a->version==CC2X_SETTINGS_VERSION&&a->size>=sizeof(*a)&&a->interface_id==CC2X_SETTINGS_ID&&
 a->number&&a->boolean&&a->set_number&&a->set_boolean&&a->reload?a:NULL;
}
#if defined(_WIN32)
#include <windows.h>
static inline const cc2x_settings_api* cc2x_settings_from_core(const cc2x_api* core){
 HMODULE module=NULL;
 if(!core||core->size<offsetof(cc2x_api,server_post)+sizeof(core->server_post)||!core->server_post||
 !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
 (LPCSTR)(uintptr_t)core->server_post,&module))return NULL;
 return cc2x_settings_resolve((cc2x_query_settings_fn)(uintptr_t)GetProcAddress(module,CC2X_SETTINGS_QUERY));
}
#endif
#endif
