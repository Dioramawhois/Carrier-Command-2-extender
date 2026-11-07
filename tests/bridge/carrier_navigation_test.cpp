// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic native branch driver. No game code or mod DLL is executed.
#include "api/carrier/motion.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <limits>
using cc2::api::carrier::Motion;
namespace {
std::array<unsigned char,0xaf7000> image{};
std::array<unsigned char,0xe80> ship{};
std::array<unsigned char,0xe80> foreign_ship{};
std::array<unsigned char,0xc80> server{};
std::array<unsigned char,0x70> peer{};
std::array<unsigned char,0xf4> tile{};
std::array<unsigned char,0x120> island{};
std::array<unsigned char,0x40> waypoint{},port{},starboard{},bow{},stern{},engine{};
std::array<unsigned char,0x38> triangle{};
std::array<unsigned char,0x70> controls{};std::array<unsigned char,4> seat{};
void* control_items[]{controls.data()};
std::array<std::array<float,4>,3> vertices{{{{-100,-5,-100,0}},{{100,-5,-100,0}},{{0,-5,100,0}}}};
void* peers[]{peer.data()};void* triangles[]{triangle.data()};
double px=0,pz=-80;int world_tiles=1;bool ocean=false,fail_path=false,fault=false,complete=false;
unsigned projected=0,filtered=0,commits=0;bool legacy=false;
Motion* controller=nullptr;unsigned reenter=0;
using Ten=void(__attribute__((thiscall))*)(void*,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t);
using Project=void(__attribute__((thiscall))*)(void*,void*,void*,void*);
using Pred=int(__cdecl*)(const void*);
using Filtered=void*(__attribute__((thiscall))*)(void*,const float*,float*,Pred);
using Nearest=void*(__attribute__((thiscall))*)(void*,const float*,float*);
using Radius=float(__attribute__((thiscall))*)(void*,void*);
std::map<std::uint32_t,void*> calls;
struct MidRecord{cc2x_mid_fn fn;void* user;};std::map<std::uint32_t,MidRecord> mids;
Radius radius=nullptr;
// Compare complete x87/MXCSR/XMM state across the naked inline callback. The
// synthetic engine deliberately leaves live FP values and nondefault rounding.
extern "C" __attribute__((naked)) int CheckFloatingState(cc2x_mid_fn,cc2x_cpu*,void*){
 __asm__("pushl %ebp\n\t" "movl %esp,%ebp\n\t" "pushl %ebx\n\t" "pushl %esi\n\t" "pushl %edi\n\t"
 "andl $-16,%esp\n\t" "subl $1536,%esp\n\t" "fxsave (%esp)\n\t"
 "movl $0x7f80,1024(%esp)\n\t" "ldmxcsr 1024(%esp)\n\t" "fnstcw 1028(%esp)\n\t" "orw $0xc00,1028(%esp)\n\t" "fldcw 1028(%esp)\n\t"
 "fld1\n\t" "fldpi\n\t" "pcmpeqd %xmm0,%xmm0\n\t" "pcmpeqd %xmm1,%xmm1\n\t" "pcmpeqd %xmm2,%xmm2\n\t" "pcmpeqd %xmm3,%xmm3\n\t"
 "pcmpeqd %xmm4,%xmm4\n\t" "pcmpeqd %xmm5,%xmm5\n\t" "pcmpeqd %xmm6,%xmm6\n\t" "pcmpeqd %xmm7,%xmm7\n\t"
 "fxsave 512(%esp)\n\t" "subl $8,%esp\n\t" "pushl 16(%ebp)\n\t" "pushl 12(%ebp)\n\t" "call *8(%ebp)\n\t" "addl $16,%esp\n\t"
 "fxsave 1024(%esp)\n\t" "leal 512(%esp),%esi\n\t" "leal 1024(%esp),%edi\n\t" "movl $72,%ecx\n\t" "repe cmpsl\n\t" "sete %al\n\t" "movzbl %al,%eax\n\t"
 "fxrstor (%esp)\n\t" "leal -12(%ebp),%esp\n\t" "popl %edi\n\t" "popl %esi\n\t" "popl %ebx\n\t" "popl %ebp\n\t" "ret\n\t");
}
template<class T>void Put(void* p,unsigned o,T v){std::memcpy(static_cast<unsigned char*>(p)+o,&v,sizeof(v));}
template<class T>T Get(const void* p,unsigned o){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+o,sizeof(v));return v;}
void Ring(void* p,unsigned o,void* data,unsigned count){Put(p,o,data);Put(p,o+4,count);Put(p,o+8,0u);Put(p,o+12,count);}
std::uintptr_t Va(std::uint32_t va){return reinterpret_cast<std::uintptr_t>(image.data())+va-0x400000;}
void Seed(std::uint32_t va,std::initializer_list<unsigned char> b){std::memcpy(reinterpret_cast<void*>(Va(va)),b.begin(),b.size());}
void SeedCall(std::uint32_t site,std::uint32_t target){unsigned char b[5]{0xe8};const auto d=target-site-5;std::memcpy(b+1,&d,4);std::memcpy(reinterpret_cast<void*>(Va(site)),b,5);}
void* Find(int list,std::uint32_t id){return list==CC2X_WORLD_VEHICLES&&id==42?ship.data():nullptr;}
std::int64_t GetI(const void*,int field,int){return field==CC2X_VEHICLE_HP?1000:0;}
int Count(const void* p,int list){return list==CC2X_WORLD_TILES?world_tiles:list==CC2X_VEHICLE_WAYPOINTS&&p==ship.data()?Get<unsigned>(ship.data(),0x14c):0;}
int Body(const void*,cc2x_body* out){*out={};out->rot[3]=1;out->pos[0]=px;out->pos[2]=pz;return 1;}
void* Server(){return server.data();}
void* Tile(double,double){return ocean?nullptr:tile.data();}
void Begin(const char*){}int Commit(){++commits;return 1;}void Abort(){}
int OriginalPred(const void* p){return Get<unsigned>(p,0x30)&2;}
void* __attribute__((thiscall)) OriginalFiltered(void*,const float* p,float* out,Pred predicate){++filtered;std::memcpy(out,p,12);return predicate(triangle.data())?triangle.data():nullptr;}
void* __attribute__((thiscall)) OriginalNearest(void*,const float* p,float* out){std::memcpy(out,p,12);return triangle.data();}
float __attribute__((thiscall)) OriginalRadius(void*,void*){return 150;}
void __attribute__((thiscall)) OriginalProject(void*,void* wp,void*,void*){++projected;Put<double>(wp,8,999);}
cc2x_cpu Mid(std::uint32_t site){cc2x_cpu cpu{};cpu.edi=reinterpret_cast<std::uint32_t>(ship.data());cpu.edx=reinterpret_cast<std::uint32_t>(triangle.data());cpu.eflags=0x246;auto r=mids.at(site);r.fn(&cpu,r.user);return cpu;}
void __attribute__((thiscall)) OriginalHelper(void*,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t){
 if(fault)RaiseException(0xe0424242,0,0,nullptr);
 // Exact native ocean path establishes initialized+success before the null-tile branch.
 if(ocean){Put<unsigned char>(ship.data(),0x224,1);Put<unsigned char>(ship.data(),0x225,1);return;}
 float p[]{10,0,0},out[3]{};auto* mesh=island.data()+0x50;
 const auto start=reinterpret_cast<Filtered>(calls.at(0x5e21bb))(mesh,p,out,OriginalPred);
 const auto end=reinterpret_cast<Nearest>(calls.at(0x5e21df))(mesh,p,out);
 auto gate=Mid(0x5e2723);assert(gate.eax==2&&gate.jump_to==Va(0x5e2729));
 auto inline_hook=mids.at(0x5e2723);assert(CheckFloatingState(inline_hook.fn,&gate,inline_hook.user));
 // Changing one vertex to land must be rejected without changing shared flags.
 const auto old=vertices[1][1];vertices[1][1]=1;
 gate=Mid(0x5e2723);assert(gate.eax==0&&(gate.eflags&0x44)==0x44);vertices[1][1]=old;
 for(float invalid:{-0.f,std::numeric_limits<float>::quiet_NaN()}){vertices[1][1]=invalid;gate=Mid(0x5e2723);assert(gate.eax==0);vertices[1][1]=old;}
 assert(Get<unsigned>(triangle.data(),0x30)==0);
 Put<unsigned char>(ship.data(),0x224,1);Put<unsigned char>(ship.data(),0x225,start&&end&&!fail_path);
 Put<double>(ship.data(),0x1d8,50);Put<double>(ship.data(),0x1e8,50);
}
void Forward(Ten fn,void* self){fn(self,1,2,3,4,5,6,7,8,9,10);}
void __attribute__((thiscall)) OriginalFollower(void* self,std::uint32_t a,std::uint32_t b,std::uint32_t c,std::uint32_t d,std::uint32_t e,std::uint32_t f,std::uint32_t g,std::uint32_t h,std::uint32_t i,std::uint32_t j){
 assert(a==1&&b==2&&c==3&&d==4&&e==5&&f==6&&g==7&&h==8&&i==9&&j==10);
 if(reenter||self==foreign_ship.data()){
  const auto action=reenter;reenter=0;
  if(action==4){Forward(reinterpret_cast<Ten>(calls.at(0x706d2c)),foreign_ship.data());return;}
  cc2x_navigation_options replacement{sizeof(replacement),CC2X_NAVIGATION_SURFACE,0,25};
  if(action==2){controller->Session(server.data(),1);controller->Tick(9);}
  else assert(controller->Set("lua.nav",1,42,false,0));
  if(action==3){RaiseException(0xe0424242,0,0,nullptr);return;}
  const auto* owner=action==1?"replacement":"lua.nav";
  assert(controller->NavigationConfigure(owner,1,42,replacement)&&controller->Set(owner,1,42,true,0));
  return; // The old invocation must never publish into this replacement.
 }
 reinterpret_cast<Project>(calls.at(0x6c23cc))(self,waypoint.data(),nullptr,nullptr);
 if(legacy){assert(radius(self,waypoint.data())==150);assert(!Get<unsigned char>(self,0x224)&&!Get<unsigned char>(self,0x225));return;}
 assert(radius(self,waypoint.data())==25);
 const auto arrival=Mid(0x6c2539);
 if(complete&&!arrival.jump_to){Put<unsigned>(ship.data(),0x14c,0);Put<unsigned char>(ship.data(),0x224,0);Put<unsigned char>(ship.data(),0x225,0);return;}
 if(!Get<unsigned char>(ship.data(),0x224))assert(arrival.jump_to==Va(0x6c2540));
 Forward(reinterpret_cast<Ten>(calls.at(0x6c2979)),self);
 if(fail_path){const auto failed=Mid(0x6c2568);assert(failed.jump_to==Va(0x6c256f));}
}
void __attribute__((thiscall)) OriginalPhysics(void*,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t){}
int PatchCall(std::uint32_t site,std::uint32_t n,const std::uint8_t* expected,void* fn,void** original){
 assert(n==5&&!std::memcmp(reinterpret_cast<void*>(Va(site)),expected,n));calls[site]=fn;
 *original=site==0x706d2c?reinterpret_cast<void*>(OriginalFollower):site==0x6c23cc?reinterpret_cast<void*>(OriginalProject):site==0x6c2979?reinterpret_cast<void*>(OriginalHelper):site==0x5e21bb?reinterpret_cast<void*>(OriginalFiltered):reinterpret_cast<void*>(OriginalNearest);return 1;
}
int PatchMid(std::uint32_t site,std::uint32_t n,const std::uint8_t* b,cc2x_mid_fn fn,void* user){assert(!std::memcmp(reinterpret_cast<void*>(Va(site)),b,n));mids[site]={fn,user};return 1;}
int PatchPtr(std::uint32_t site,std::uint32_t,void* fn,void** original){if(site==0xd596ec+0x34){radius=reinterpret_cast<Radius>(fn);*original=reinterpret_cast<void*>(OriginalRadius);}else *original=reinterpret_cast<void*>(OriginalPhysics);return 1;}
void ResetRoute(){Put<unsigned>(ship.data(),0x14c,1);Put<unsigned>(waypoint.data(),0,1);Put<unsigned>(waypoint.data(),4,0);Put<double>(waypoint.data(),8,10);Put<double>(waypoint.data(),0x10,0);complete=fail_path=fault=false;px=0;pz=-80;}
cc2x_api Make(){cc2x_api api{};api.game_build="1.5.18-win32-r2";api.va=Va;api.find=Find;api.geti=GetI;api.count=Count;api.body_get=Body;api.server=Server;api.tile_at=Tile;
 api.patch_begin=Begin;api.patch_commit=Commit;api.patch_abort=Abort;api.patch_call=PatchCall;api.patch_mid=PatchMid;api.patch_ptr=PatchPtr;
 Seed(0x706c90,{0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x58});Seed(0x706cd4,{0x8b,0x87,0xd0,1,0,0,0x85,0xc0,0x0f,0x85,0xc4,2,0,0});
 Seed(0x77d4e4,{0xff,0xb6,0xd4,0xd,0,0,0x8d,0x8e,0,5,0,0,0x57});Seed(0x707429,{0x5f,0x5e,0x8b,0xe5,0x5d,0xc2,0x1c,0});
 for(auto p:{std::pair{0x706d2cu,0x6c2330u},{0x6c23ccu,0x6c7770u},{0x6c2979u,0x6c7920u},{0x5e21bbu,0x5e1780u},{0x5e21dfu,0x5e15f0u}})SeedCall(p.first,p.second);
 Seed(0x5e2723,{0x8b,0x42,0x30,0x83,0xe0,2});Seed(0x6c2539,{0xc6,0x87,0x26,2,0,0,1});Seed(0x6c2568,{0xc6,0x87,0x26,2,0,0,1});
 Seed(0x6c4420,{0xc2,0x28,0});Seed(0x6c824a,{0xc2,0x28,0});Seed(0x6c791d,{0xc2,0x0c,0});Seed(0x708296,{0xc2,4,0});
 Seed(0x5e173e,{0xc2,0x08,0x00});
 Seed(0x5e176e,{0xc2,0x08,0x00});
 Seed(0x5e18ca,{0xc2,0x0c,0x00});
 Seed(0x5e18f6,{0xc2,0x0c,0x00});
 Seed(0x5e21b2,{0x68,0x10,0x22,0x5e,0x00,0x50,0x51,0x8b,0xce,0xe8,0xc0,0xf5,0xff,0xff,0xeb,0x10,0x8b,0x75,0x08,0x8d,0x44,0x24,0x0c,0x50,0x51,0x8b,0xce,0xe8,0x1e,0xf4,0xff,0xff,0x8b,0xf8,0x8b,0xce,0x8d,0x44,0x24,0x0c,0x50,0xff,0x74,0x24,0x0c,0xe8,0x0c,0xf4,0xff,0xff,0x85,0xff,0x74,0x20,0x85,0xc0,0x74,0x1c,0xff,0x75,0x14,0x8b,0xd0,0x8b,0xcf,0xff,0x75,0x10,0xff,0x75,0x0c,0x56,0xe8,0x31,0x00,0x00,0x00,0x83,0xc4,0x10,0x5f,0x5e,0x8b,0xe5,0x5d,0xc3,0x5f,0x32,0xc0,0x5e,0x8b,0xe5,0x5d,0xc3});
 Seed(0x5e2729,{0x75,0x2e,0xc6,0x42,0x10,0x01});
 Seed(0x6c23b6,{0x56,0xff,0x73,0x0c,0x0f,0x42,0xd1,0x8b,0xcf,0xc1,0xe2,0x06,0x03,0x97,0x40,0x01,0x00,0x00,0x52,0x89,0x55,0xec,0xe8,0x9f,0x53,0x00,0x00,0x8b,0x45,0xec,0x8b,0x55,0xd0});
 Seed(0x6c295a,{0x0f,0x10,0x06,0x56,0xff,0x73,0x0c,0x51,0xff,0x73,0x10,0x8b,0xcf,0x83,0xec,0x18,0x8b,0xc4,0x0f,0x11,0x00,0xf3,0x0f,0x7e,0x46,0x10,0x66,0x0f,0xd6,0x40,0x10,0xe8,0xa2,0x4f,0x00,0x00,0xe9,0x8a,0x1a,0x00,0x00});
 Seed(0x706ce2,{0x8d,0x44,0x24,0x48,0xc6,0x87,0x50,0x03,0x00,0x00,0x01,0x50,0x8d,0x87,0xf8,0x01,0x00,0x00,0xf2,0x0f,0x11,0x44,0x24,0x5c,0x50,0x8d,0x87,0xf0,0x01,0x00,0x00,0xc7,0x44,0x24,0x24,0x00,0x00,0x00,0x00,0x50,0x8d,0x44,0x24,0x28,0x50,0x8d,0x8f,0xd8,0x01,0x00,0x00,0x51,0xff,0x75,0x18,0x8b,0xcf,0xff,0x75,0x1c,0x0f,0x11,0x44,0x24,0x64,0xff,0x75,0x14,0xff,0x75,0x0c,0xff,0x75,0x10,0xe8,0xff,0xb5,0xfb,0xff,0x8b,0x87,0x94,0x00,0x00,0x00});
 Seed(0x708290,{0xd9,0x05,0x40,0x7a,0xdb,0x00,0xc2,0x04,0x00});
 Put(ship.data(),0,Va(0xd596ec));Put<unsigned>(ship.data(),0x80,42);Ring(ship.data(),0x140,waypoint.data(),1);
 Ring(server.data(),0xc68,peers,1);Put<unsigned>(peer.data(),0x68,0);
 Put(tile.data(),0xf0,island.data());Ring(island.data(),0x50,triangles,1);Ring(island.data(),0x60,vertices.data(),3);
 Put<unsigned>(triangle.data(),0x14,0);Put<unsigned>(triangle.data(),0x18,1);Put<unsigned>(triangle.data(),0x1c,2);
 Put(ship.data(),0xc30,port.data());Put(ship.data(),0xc34,starboard.data());Put(ship.data(),0xc38,bow.data());Put(ship.data(),0xc3c,stern.data());Put(ship.data(),0xa58,engine.data());
 ResetRoute();return api;
}
}
int main(){
 auto api=Make();Motion motion;controller=&motion;motion.Configure(&api);motion.Session(server.data(),1);motion.Tick(1);
 cc2x_navigation_options options{sizeof(options),CC2X_NAVIGATION_SURFACE,0,25};
 assert(!motion.NavigationConfigure("lua.nav",1,42,options));
 const std::uint32_t sites[]{0x706d2c,0x6c23cc,0x6c2979,0x5e21bb,0x5e21df,0x5e2723,0x6c2539,0x6c2568,0x6c4420,0x6c824a,0x6c791d,0x708296,
  0x706ce2,0x6c23b6,0x6c295a,0x5e21b2,0x708290,0x5e2729,0x5e173e,0x5e176e,0x5e18ca,0x5e18f6};
 for(auto site:sites){auto* p=reinterpret_cast<unsigned char*>(Va(site));*p^=1;assert(!motion.InstallNavigation());*p^=1;}
 assert(!commits&&motion.InstallNavigation()&&motion.NavigationAvailable());
 assert(motion.NavigationConfigure("lua.nav",1,42,options));
 cc2x_navigation_state state{sizeof(state)};assert(motion.NavigationRead(42,&state));assert(state.flags==CC2X_NAVIGATION_CONFIGURED);
 assert(!motion.NavigationConfigure("lua.other",1,42,options));
 auto bad=options;bad.minimum_depth=NAN;assert(!motion.NavigationConfigure("lua.nav",1,42,bad));bad=options;bad.arrival_radius=0;assert(!motion.NavigationConfigure("lua.nav",1,42,bad));
 // Pending configuration cannot reserve a carrier forever without a lease.
 motion.Tick(4);assert(!motion.Set("legacy",1,42,true,0));motion.Tick(5);
 assert(motion.NavigationRead(42,&state)&&!state.flags);assert(motion.Set("legacy",1,42,true,0));assert(motion.Set("legacy",1,42,false,0));
 assert(motion.NavigationConfigure("lua.nav",1,42,options));
 Put<unsigned>(seat.data(),0,7);Put(ship.data(),0x390,seat.data());Put<unsigned>(controls.data(),0,7);Put<float>(controls.data(),0x50,1);
 std::array<unsigned char,16> inputs{};Ring(inputs.data(),0,control_items,1);motion.ObserveControls(ship.data(),inputs.data());
 assert(motion.NavigationRead(42,&state)&&!state.flags);assert(!motion.NavigationConfigure("lua.nav",1,42,options));
 Put<float>(controls.data(),0x50,0);motion.ObserveControls(ship.data(),inputs.data());assert(motion.NavigationConfigure("lua.nav",1,42,options));
 assert(motion.Set("lua.nav",1,42,true,0));Forward(reinterpret_cast<Ten>(calls.at(0x706d2c)),ship.data());
 assert(!projected&&filtered==2&&Get<double>(waypoint.data(),8)==10);
 assert(motion.NavigationRead(42,&state)&&(state.flags&CC2X_NAVIGATION_READY));assert(state.target_x==10&&state.target_z==0&&state.distance>25);
 assert(radius(ship.data(),waypoint.data())==150);auto outside=Mid(0x5e2723);assert(outside.jump_to==0); // Scoped context restored.
 // Release must invalidate the shallow cache before an unchanged v1 owner.
 assert(Get<unsigned char>(ship.data(),0x224)&&Get<unsigned char>(ship.data(),0x225));
 assert(motion.Set("lua.nav",1,42,false,0));assert(!Get<unsigned char>(ship.data(),0x224)&&!Get<unsigned char>(ship.data(),0x225));assert(Get<double>(waypoint.data(),8)==10);
 legacy=true;assert(motion.Set("legacy",1,42,true,0));Forward(reinterpret_cast<Ten>(calls.at(0x706d2c)),ship.data());assert(projected==1);assert(motion.Set("legacy",1,42,false,0));legacy=false;projected=0;ResetRoute();
 assert(motion.NavigationConfigure("lua.nav",1,42,options)&&motion.Set("lua.nav",1,42,true,0));Forward(reinterpret_cast<Ten>(calls.at(0x706d2c)),ship.data());
 assert(motion.NavigationConfigure("lua.nav",1,42,options));complete=true;px=10;pz=0;
 Forward(reinterpret_cast<Ten>(calls.at(0x706d2c)),ship.data());assert(motion.NavigationRead(42,&state));assert((state.flags&CC2X_NAVIGATION_ARRIVED)&&!(state.flags&CC2X_NAVIGATION_BLOCKED));
 motion.BeforeMotion(ship.data());assert(motion.NavigationRead(42,&state)&&(state.flags&CC2X_NAVIGATION_ARRIVED)&&!(state.flags&CC2X_NAVIGATION_ACTIVE));motion.Tick(9);assert(motion.NavigationRead(42,&state)&&!state.flags);
 assert(motion.Set("lua.nav",1,42,false,0));ResetRoute();assert(motion.NavigationConfigure("lua.nav",1,42,options));assert(motion.Set("lua.nav",1,42,true,0));
 fail_path=true;Forward(reinterpret_cast<Ten>(calls.at(0x706d2c)),ship.data());assert(motion.NavigationRead(42,&state)&&(state.flags&CC2X_NAVIGATION_BLOCKED));assert(Count(ship.data(),CC2X_VEHICLE_WAYPOINTS)==1);assert(!motion.Drive("lua.nav",1,42,1,1));
 for(auto* jet:{port.data(),starboard.data(),bow.data(),stern.data()})Put<float>(jet,0x30,1);
 motion.AfterMotion(ship.data());for(auto* jet:{port.data(),starboard.data(),bow.data(),stern.data()})assert(Get<float>(jet,0x30)==0);
 assert(motion.Set("lua.nav",1,42,false,0));ResetRoute();ocean=true;
 assert(motion.NavigationConfigure("lua.nav",1,42,options)&&motion.Set("lua.nav",1,42,true,0));Forward(reinterpret_cast<Ten>(calls.at(0x706d2c)),ship.data());assert(motion.NavigationRead(42,&state)&&(state.flags&CC2X_NAVIGATION_READY));
 assert(motion.Set("lua.nav",1,42,false,0));ResetRoute();world_tiles=0;
 assert(motion.NavigationConfigure("lua.nav",1,42,options)&&motion.Set("lua.nav",1,42,true,0));Forward(reinterpret_cast<Ten>(calls.at(0x706d2c)),ship.data());assert(motion.NavigationRead(42,&state)&&(state.flags&CC2X_NAVIGATION_BLOCKED));
 assert(motion.Set("lua.nav",1,42,false,0));ResetRoute();world_tiles=1;ocean=false;vertices[1][1]=1;
 assert(motion.NavigationConfigure("lua.nav",1,42,options)&&motion.Set("lua.nav",1,42,true,0));Forward(reinterpret_cast<Ten>(calls.at(0x706d2c)),ship.data());assert(motion.NavigationRead(42,&state)&&(state.flags&CC2X_NAVIGATION_BLOCKED));assert(!projected);
 assert(motion.Set("lua.nav",1,42,false,0));ResetRoute();vertices[1][1]=-5;
 // Native reentrancy must not let an old invocation overwrite or Stop a new
 // lease, even when a Session reset reuses the same generation and carrier.
 Put<unsigned>(foreign_ship.data(),0x80,43);
 for(unsigned action:{1u,2u,4u}){
  assert(motion.NavigationConfigure("lua.nav",1,42,options)&&motion.Set("lua.nav",1,42,true,0));reenter=action;
  Forward(reinterpret_cast<Ten>(calls.at(0x706d2c)),ship.data());assert(motion.NavigationRead(42,&state));
  assert(state.flags==(CC2X_NAVIGATION_CONFIGURED|CC2X_NAVIGATION_ACTIVE)&&state.waypoint_id==0&&state.target_x==0&&Get<unsigned>(ship.data(),0x1d0)==0);
  assert(radius(ship.data(),waypoint.data())==150&&Mid(0x5e2723).jump_to==0);
  assert(motion.Set(action==1?"replacement":"lua.nav",1,42,false,0));ResetRoute();
 }
 reenter=3; // A native fault after cancellation still disables the capability.
 assert(motion.NavigationConfigure("lua.nav",1,42,options)&&motion.Set("lua.nav",1,42,true,0));Forward(reinterpret_cast<Ten>(calls.at(0x706d2c)),ship.data());
 assert(!motion.NavigationAvailable()&&motion.NavigationRead(42,&state)&&state.flags==CC2X_NAVIGATION_FAULTED);assert(radius(ship.data(),waypoint.data())==150&&Mid(0x5e2723).jump_to==0);
 assert(motion.Set("lua.nav",1,42,false,0));motion.Session(nullptr,2);
 std::cout<<"PASS coastal navigation: shallow water, land refusal, scoped hooks, raw endpoint,25m completion, failed path, ocean, missing world, SEH cleanup\n";
}
