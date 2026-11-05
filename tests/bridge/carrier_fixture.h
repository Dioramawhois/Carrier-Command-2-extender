#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic engine only: request lifetimes, hook budgets and register edits.
#include "api/carrier/recovery.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
using cc2::api::carrier::Recovery;
namespace {
// Test the actual assembly wrapper with all eight XMM registers live.
__attribute__((naked)) void XmmCall(cc2x_mid_fn,cc2x_cpu*,void*,const void*,void*){
 __asm__("pushl %ebx\n\t" "pushl %esi\n\t" "pushl %edi\n\t" "movl 28(%esp), %eax\n\t"
 "movups 0x00(%eax), %xmm0\n\t" "movups 0x10(%eax), %xmm1\n\t" "movups 0x20(%eax), %xmm2\n\t" "movups 0x30(%eax), %xmm3\n\t"
 "movups 0x40(%eax), %xmm4\n\t" "movups 0x50(%eax), %xmm5\n\t" "movups 0x60(%eax), %xmm6\n\t" "movups 0x70(%eax), %xmm7\n\t"
 "pushl 24(%esp)\n\t" "pushl 24(%esp)\n\t" "call *24(%esp)\n\t" "addl $8, %esp\n\t" "movl 32(%esp), %eax\n\t"
 "movups %xmm0, 0x00(%eax)\n\t" "movups %xmm1, 0x10(%eax)\n\t" "movups %xmm2, 0x20(%eax)\n\t" "movups %xmm3, 0x30(%eax)\n\t"
 "movups %xmm4, 0x40(%eax)\n\t" "movups %xmm5, 0x50(%eax)\n\t" "movups %xmm6, 0x60(%eax)\n\t" "movups %xmm7, 0x70(%eax)\n\t"
 "popl %edi\n\t" "popl %esi\n\t" "popl %ebx\n\t" "ret\n\t");
}
unsigned char image[0xaf7000]{},ship[0xe00]{},plane[0x600]{},server[0x400]{},manager[0xe0]{};
unsigned char bays[0x70]{},model[0xf0]{},nodes[64]{},wheels[0x70]{};
int dock=0,mode=0,team=1,calls=0,patches=0,commits=0,aborts=0,wing=1;
bool valid=true,commit=true;cc2x_mid_fn hooks[4]{};void* users[4]{};void(*duringDock)()=nullptr;
Recovery* inCall=nullptr;uint64_t nestedRequest=0;
template<class T> void Put(void* p,int o,T v){std::memcpy(static_cast<unsigned char*>(p)+o,&v,sizeof(v));}
uintptr_t Va(uint32_t address){return reinterpret_cast<uintptr_t>(image)+address-0x400000;}
void* Find(int list,uint32_t id){return !valid||list!=CC2X_WORLD_VEHICLES?nullptr:id==1?ship:id==7?plane:nullptr;}
int64_t GetI(const void* p,int f,int){if(p==ship)return f==CC2X_VEHICLE_DEFINITION?0:f==CC2X_VEHICLE_TEAM?1:0;
 if(p!=plane)return 0;switch(f){case CC2X_VEHICLE_IS_AIR:return 1;case CC2X_VEHICLE_IS_WING:return wing;
 case CC2X_VEHICLE_IS_ROTOR:return !wing;case CC2X_VEHICLE_TEAM:return team;case CC2X_VEHICLE_DOCK_STATE:return dock;
 case CC2X_VEHICLE_DOCK_VEHICLE:return dock?1:0;case CC2X_VEHICLE_CONTROL_MODE:return mode;default:return 0;}}
int Count(const void* p,int f){return p==plane&&f==CC2X_VEHICLE_WHEELS?1:0;}
double GetF(const void* p,int f,int){return p==plane&&f==CC2X_VEHICLE_WHEEL_CONTACT?1:0;}
int Body(const void*,cc2x_body* b){*b={};return 1;}
void* Server(){return server;}
void Begin(const char*){patches=0;}
int Mid(uint32_t,uint32_t,const uint8_t*,cc2x_mid_fn fn,void* user){hooks[patches]=fn;users[patches++]=user;return 1;}
int Commit(){++commits;return commit;}
void Abort(){++aborts;}
void Dock(uintptr_t,void*,void* world,uint32_t id){assert(world==server+0x344&&id==7);++calls;dock=5;if(duringDock)duringDock();}
void Fault(uintptr_t,void*,void*,uint32_t){RaiseException(0xe0424242,0,0,nullptr);}
cc2x_api Make(){cc2x_api a{};a.game_build="1.5.18-win32-r2";a.module_base=reinterpret_cast<uintptr_t>(image);
 a.va=Va;a.find=Find;a.geti=GetI;a.getf=GetF;a.count=Count;a.body_get=Body;a.server=Server;
 a.patch_begin=Begin;a.patch_mid=Mid;a.patch_commit=Commit;a.patch_abort=Abort;
 auto*d=reinterpret_cast<IMAGE_DOS_HEADER*>(image);d->e_magic=IMAGE_DOS_SIGNATURE;d->e_lfanew=0x80;
 auto*n=reinterpret_cast<IMAGE_NT_HEADERS32*>(image+0x80);n->Signature=IMAGE_NT_SIGNATURE;n->FileHeader.Machine=IMAGE_FILE_MACHINE_I386;
 n->FileHeader.TimeDateStamp=0x6ae228f3;n->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC;n->OptionalHeader.SizeOfImage=sizeof(image);
 // Exact signatures in the checked profile; intentionally corrupt them below.
 auto seed=[&](uint32_t va,std::initializer_list<unsigned char> b){std::memcpy(reinterpret_cast<void*>(Va(va)),b.begin(),b.size());};
 seed(0x707440,{0x55,0x8b,0xec,0x51,0x56,0x57,0xff,0x75,0x0c,0x8b,0xf1,0x8b,0x4d,0x08});
 seed(0x7019de,{0x8b,0x40,0x04,0x83,0xf8,0x05});seed(0x701b4d,{0x8d,0x85,0x28,0xfd,0xff,0xff});
 seed(0x6fe580,{0xc6,0x85,0xfb,0xfd,0xff,0xff,0x01});seed(0x6fdded,{0xf3,0x0f,0x59,0xd2,0xf3,0x0f,0x59,0xdb});
 seed(0x701b9a,{0xff,0xb5,0xfc,0xfd,0xff,0xff,0x8d,0x4f,0x34});
 seed(0x6ff31a,{0xf3,0x0f,0x10,0x95,0x1c,0xfd,0xff,0xff,0xf3,0x0f,0x10,0x85,0xcc,0xfd,0xff,0xff});
 seed(0x6fde02,{0x8b,0x8d,0x88,0xfd,0xff,0xff,0x03,0x4f,0x3c});Put(reinterpret_cast<void*>(Va(0xd596ec)),0x7c,static_cast<uint32_t>(Va(0x707440)));
 Put(ship,0,Va(0xd596ec));Put(ship,0x154,bays);Put(ship,0x158,1u);Put(ship,0x88,model);
 Put(model,0xe0,nodes);Put(model,0xe4,1u);Put(nodes,0x3c,1u);
 Put(plane,0xac,wheels);Put(plane,0xb0,1u);Put(wheels,0,1.0f);
 return a;}
}
