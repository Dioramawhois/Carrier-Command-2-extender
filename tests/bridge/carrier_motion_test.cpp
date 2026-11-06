// SPDX-License-Identifier: GPL-3.0-or-later
#include "api/carrier/motion.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
using cc2::api::carrier::Motion;
namespace {
std::array<unsigned char,0xe80> ship{},enemy{},missile{},server{};
std::array<unsigned char,0x140> team{};
std::array<unsigned char,0x80> observation{};
std::array<unsigned char,0x70> peer{};
std::array<unsigned char,0x58> input{};
std::array<unsigned char,0x40> port{},starboard{};
std::array<unsigned char,12> missileVision{};
void* peerList[1]{peer.data()};void* teamList[1]{team.data()};void* missileList[1]{missile.data()};void* inputList[1]{input.data()};
std::uint32_t seat=3;std::uintptr_t vtable[64]{};
template<class T>void put(void* p,unsigned off,T value){std::memcpy(static_cast<unsigned char*>(p)+off,&value,sizeof(value));}
template<class T>T get(const void* p,unsigned off){T value{};std::memcpy(&value,static_cast<const unsigned char*>(p)+off,sizeof(value));return value;}
void ring(void* p,unsigned off,void* base,unsigned count){put(p,off,base);put(p,off+4,count);put(p,off+8,0u);put(p,off+12,count);}
std::map<std::uint32_t,std::vector<unsigned char>> guards{
 {0x706c90,{0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x58}},
 {0x706cd4,{0x8b,0x87,0xd0,1,0,0,0x85,0xc0,0x0f,0x85,0xc4,2,0,0}},
 {0x77d4e4,{0xff,0xb6,0xd4,0xd,0,0,0x8d,0x8e,0,5,0,0,0x57}},
 {0x707429,{0x5f,0x5e,0x8b,0xe5,0x5d,0xc2,0x1c,0}}};
std::uintptr_t address(std::uint32_t va){return va==0xd596ec?reinterpret_cast<std::uintptr_t>(vtable):reinterpret_cast<std::uintptr_t>(guards[va].data());}
void* find(int list,std::uint32_t id){return list==CC2X_WORLD_VEHICLES?(id==42?ship.data():id==70?enemy.data():nullptr):nullptr;}
std::int64_t geti(const void* p,int field,int){switch(field){case CC2X_VEHICLE_TEAM:return get<std::uint32_t>(p,0x108);case CC2X_VEHICLE_HP:return 1000;case CC2X_VEHICLE_DEFINITION:return 5;default:return 0;}}
int count(const void*,int list){return list==CC2X_VEHICLE_WAYPOINTS?2:0;}
int body(const void*,cc2x_body* out){*out={};out->vel[2]=2;out->pos[2]=100;return 1;}
using Physics=void(__attribute__((thiscall)) *)(void*,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t);
Physics hook=nullptr;
void __attribute__((thiscall)) originalPhysics(void*,std::uint32_t a,std::uint32_t b,std::uint32_t c,std::uint32_t d,std::uint32_t e,std::uint32_t f,std::uint32_t g){assert(a==1&&b==2&&c==3&&d==4&&e==5&&f==6&&g==7);}
void begin(const char*){}int commit(){return 1;}void abortPatch(){}
int patch(std::uint32_t site,std::uint32_t expected,void* replacement,void** saved){assert(site==0xd596ec+57*4&&expected==0x706c90);hook=reinterpret_cast<Physics>(replacement);*saved=reinterpret_cast<void*>(&originalPhysics);return 1;}
}
int main(){
 cc2x_api api{};api.game_build="1.5.18-win32-r2";api.va=address;api.find=find;api.geti=geti;api.count=count;api.body_get=body;
 api.patch_begin=begin;api.patch_ptr=patch;api.patch_commit=commit;api.patch_abort=abortPatch;
 put(ship.data(),0,reinterpret_cast<std::uintptr_t>(vtable));put(ship.data(),0x80,42u);put(ship.data(),0x1d0,1u);
 put(ship.data(),0xc30,port.data());put(ship.data(),0xc34,starboard.data());put(ship.data(),0x390,&seat);
 put(input.data(),0,3u);std::array<unsigned char,16> inputs{};ring(inputs.data(),0,inputList,1);
 ring(server.data(),0xc68,peerList,1);ring(server.data(),0x504,teamList,1);
 ring(team.data(),0x1c,observation.data(),1);ring(team.data(),0x2c,missileVision.data(),1);
 ring(server.data(),0x3ec,missileList,1);
 put(observation.data(),0,70u);put(observation.data(),0x18,(unsigned char)1);put(enemy.data(),0x108,1u);
 put(missileVision.data(),0,100u);put(missileVision.data(),8,(unsigned char)1);
 put(missile.data(),0,100u);put(missile.data(),4,10u);put(missile.data(),8,1u);put(missile.data(),0x78,400.0);
 Motion motion;motion.Configure(&api);motion.Session(server.data(),1);motion.Tick(1);
 assert(motion.Available());cc2x_motion_state state{sizeof(state)};assert(motion.Read(42,&state));assert(state.flags&CC2X_MOTION_LOCAL);
 assert(!motion.Set("lua.test",2,42,true,.5));assert(!motion.Set("lua.test",1,42,true,NAN));
 assert(motion.Set("lua.test",1,42,true,.4));assert(get<unsigned>(ship.data(),0x1d0)==0);
 assert(hook);hook(ship.data(),1,2,3,4,5,6,7);
 assert(!motion.Set("lua.other",1,42,true,.4));
 put(port.data(),0x30,.8f);put(starboard.data(),0x30,-.8f);motion.AfterMotion(ship.data());
 assert(std::abs(get<float>(port.data(),0x30)-.4f)<1e-6);assert(std::abs(get<float>(starboard.data(),0x30)+.4f)<1e-6);
 put(input.data(),0x50,1.f);motion.ObserveControls(ship.data(),inputs.data());assert(get<unsigned>(ship.data(),0x1d0)==1);
 assert(!motion.Set("lua.test",1,42,true,.4));put(input.data(),0x50,0.f);motion.ObserveControls(ship.data(),inputs.data());
 assert(motion.Set("lua.test",1,42,true,.4));motion.Tick(5);motion.BeforeMotion(ship.data());assert(get<unsigned>(ship.data(),0x1d0)==1);
 assert(motion.Set("lua.test",1,42,true,.4));motion.DropOwner("lua.test");assert(get<unsigned>(ship.data(),0x1d0)==1);
 std::vector<cc2x_contact> contacts;assert(motion.Contacts(42,&contacts));assert(contacts.size()==2&&contacts[1].id==100);
 put(observation.data(),0x18,(unsigned char)0);put(missileVision.data(),8,(unsigned char)0);
 assert(motion.Contacts(42,&contacts)&&contacts.empty());
 put(peer.data(),0x68,2u);assert(!motion.Set("lua.test",1,42,true,.4));assert(!motion.Contacts(42,&contacts));
 motion.Session(nullptr,1);assert(!motion.Read(42,&state));
 std::cout<<"PASS carrier motion: guarded profile, ownership, peer team, power cap, takeover, expiry, contacts, session\n";
}
