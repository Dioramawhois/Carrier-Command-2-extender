// SPDX-License-Identifier: GPL-3.0-or-later
#include "motion.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
namespace cc2::api::carrier {
namespace {
template<class T>T At(const void* p,std::size_t off){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof(v));return v;}
template<class T>void Put(void* p,std::size_t off,T v){std::memcpy(static_cast<unsigned char*>(p)+off,&v,sizeof(v));}
bool Region(const void* p,std::size_t bytes,bool write=false){
 MEMORY_BASIC_INFORMATION m{};if(!p||!bytes||!VirtualQuery(p,&m,sizeof(m))||m.State!=MEM_COMMIT||(m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
 auto a=reinterpret_cast<std::uintptr_t>(p),b=reinterpret_cast<std::uintptr_t>(m.BaseAddress);
 if(a<b||a-b>m.RegionSize||bytes>m.RegionSize-(a-b))return false;
 const auto access=m.Protect&0xff;
 return !write||access==PAGE_READWRITE||access==PAGE_WRITECOPY||access==PAGE_EXECUTE_READWRITE||access==PAGE_EXECUTE_WRITECOPY;
}
struct Ring {unsigned char* data;std::uint32_t cap,head,count;};
bool RingAt(const void* p,std::size_t off,std::size_t stride,std::uint32_t limit,Ring* out){
 if(!Region(static_cast<const unsigned char*>(p)+off,16))return false;
 Ring q=At<Ring>(p,off);
 if(q.count>q.cap||q.cap>limit||(q.cap&&(q.head>=q.cap||!Region(q.data,q.cap*stride))))return false;
 if(q.count&&!q.cap)return false;*out=q;return true;
}
unsigned char* Item(const Ring& q,std::uint32_t i,std::size_t stride){return q.data+((q.head+i)%q.cap)*stride;}
Motion* installed=nullptr;
using Physics=void(__attribute__((thiscall)) *)(void*,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t);
Physics original=nullptr;
void __fastcall PhysicsHook(void* self,void*,std::uint32_t a,std::uint32_t b,std::uint32_t c,std::uint32_t d,std::uint32_t e,std::uint32_t f,std::uint32_t g){
 if(installed)installed->BeforeMotion(self);
 original(self,a,b,c,d,e,f,g);
 if(installed)installed->AfterMotion(self);
}
bool Guard(const cc2x_api* a,std::uint32_t address,const unsigned char* bytes,std::size_t size){
 auto* p=reinterpret_cast<const void*>(a->va(address));return Region(p,size)&&std::memcmp(p,bytes,size)==0;
}
}
void Motion::Configure(const cc2x_api* api){api_=api;}
void Motion::Session(void* server,std::uint64_t generation){server_=server;generation_=server?generation:0;tick_=0;thread_=0;leases_.clear();manual_.clear();}
void Motion::Tick(std::uint64_t tick){if(!server_)return;thread_=GetCurrentThreadId();tick_=tick;}
bool Motion::Context()const{return server_&&generation_&&thread_==GetCurrentThreadId()&&api_&&api_->find&&api_->geti&&api_->count&&api_->body_get;}
bool Motion::Available()const{
 if(!api_||!api_->game_build||std::strcmp(api_->game_build,"1.5.18-win32-r2")||!api_->va)return false;
 constexpr unsigned char entry[]{0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x58};
 constexpr unsigned char mode[]{0x8b,0x87,0xd0,1,0,0,0x85,0xc0,0x0f,0x85,0xc4,2,0,0};
 constexpr unsigned char vision[]{0xff,0xb6,0xd4,0x0d,0,0,0x8d,0x8e,0,5,0,0,0x57};
 constexpr unsigned char exit[]{0x5f,0x5e,0x8b,0xe5,0x5d,0xc2,0x1c,0};
 return Guard(api_,0x706c90,entry,sizeof(entry))&&Guard(api_,0x706cd4,mode,sizeof(mode))&&Guard(api_,0x77d4e4,vision,sizeof(vision))&&Guard(api_,0x707429,exit,sizeof(exit));
}
bool Motion::Install(){
 if(installed_)return true;
 if(!Available()||installed||!api_->patch_ptr||!api_->patch_begin||!api_->patch_abort||!api_->patch_commit)return false;
 api_->patch_begin("cc2x.motion");void* saved=nullptr;
 if(!api_->patch_ptr(0xd596ec+57*4,0x706c90,reinterpret_cast<void*>(&PhysicsHook),&saved)){api_->patch_abort();return false;}
 original=reinterpret_cast<Physics>(saved);installed=this;
 if(!api_->patch_commit()){installed=nullptr;original=nullptr;api_->patch_abort();return false;}
 installed_=true;if(api_->log)api_->log("motion",CC2X_LOG_INFO,"event=MOTION_HOOK_READY; native steering and guarded forward-power cap");return true;
}
unsigned char* Motion::Carrier(std::uint32_t id)const{
 if(!Context()||!Available())return nullptr;
 auto* v=static_cast<unsigned char*>(api_->find(CC2X_WORLD_VEHICLES,id));
 if(!Region(v,0xe7c,true)||At<std::uintptr_t>(v,0)!=api_->va(0xd596ec)||api_->geti(v,CC2X_VEHICLE_TEAM_IS_AI,0)||api_->geti(v,CC2X_VEHICLE_HP,0)<=0)return nullptr;
 return v;
}
bool Motion::LocalTeam(std::uint32_t* team)const{
 // Exact engine peer inventory; first candidate deliberately refuses remote play.
 if(!Context()||!Region(server_,0xc78))return false;Ring peers;
 if(!RingAt(server_,0xc68,4,4096,&peers)||peers.count!=1)return false;
 auto* peer=At<unsigned char*>(Item(peers,0,4),0);
 if(!Region(peer,0x6c))return false;*team=At<std::uint32_t>(peer,0x68);return true;
}
bool Motion::Read(std::uint32_t id,cc2x_motion_state* out){
 auto* v=Carrier(id);if(!v||!out||out->size!=sizeof(*out))return false;
 cc2x_body b{};if(!api_->body_get(v,&b))return false;
 cc2x_motion_state s{sizeof(s),id,static_cast<std::uint32_t>(api_->geti(v,CC2X_VEHICLE_TEAM,0)),0,
  At<std::uint32_t>(v,0x1d0),static_cast<std::uint32_t>(api_->count(v,CC2X_VEHICLE_WAYPOINTS)),generation_};
 std::uint32_t team=0;if(LocalTeam(&team)&&team==s.team)s.flags|=CC2X_MOTION_LOCAL;
 if(leases_.count(id))s.flags|=CC2X_MOTION_ACTIVE;
 if(manual_[id])s.flags|=CC2X_MOTION_MANUAL;
 if(At<unsigned char>(v,0x350))s.flags|=CC2X_MOTION_ENGINE;
 s.speed=std::sqrt(b.vel[0]*b.vel[0]+b.vel[2]*b.vel[2]);s.lever=At<float>(v,0xc94);
 s.forward_power=0;auto* jet=At<unsigned char*>(v,0xc30);if(Region(jet,0x34))s.forward_power=At<float>(jet,0x30);
 s.target_x=At<double>(v,0x1d8);s.target_z=At<double>(v,0x1e8);
 if(!std::isfinite(s.speed)||!std::isfinite(s.lever)||!std::isfinite(s.forward_power))return false;*out=s;return true;
}
void Motion::Stop(unsigned char* v){Put<std::uint32_t>(v,0x1d0,1);Put<float>(v,0xc94,0);Put<float>(v,0xc90,0);}
bool Motion::Set(const std::string& owner,std::uint64_t generation,std::uint32_t id,bool active,double power){
 if(!Context()||generation!=generation_||owner.empty()||owner.size()>96||!std::isfinite(power)||power<0||power>1)return false;
 auto* v=Carrier(id);std::uint32_t team=0;if(!v||!LocalTeam(&team)||team!=api_->geti(v,CC2X_VEHICLE_TEAM,0))return false;
 auto found=leases_.find(id);if(found!=leases_.end()&&found->second.owner!=owner)return false;
 if(!active){if(found==leases_.end())return true;Stop(v);leases_.erase(found);return true;}
 if(manual_[id]||api_->count(v,CC2X_VEHICLE_WAYPOINTS)<=0||!Install())return false;
 if(leases_.size()>=8&&found==leases_.end())return false;
 leases_[id]={owner,tick_,power};Put<std::uint32_t>(v,0x1d0,0);return true;
}
void Motion::ObserveControls(void* carrier,void* inputs){
 if(!Context()||!Region(carrier,0xc98))return;
 auto id=At<std::uint32_t>(carrier,0x80);bool manual=false;auto* seat=At<unsigned char*>(carrier,0x390);
 if(Region(seat,4)){auto player=At<std::uint32_t>(seat,0);Ring ring;
  if(player&&RingAt(inputs,0,4,256,&ring))for(std::uint32_t i=0;i<ring.count;++i){auto* input=At<unsigned char*>(Item(ring,i,4),0);
   if(!Region(input,0x58)||At<std::uint32_t>(input,0)!=player)continue;
   auto steer=At<float>(input,0x50),throttle=At<float>(input,0x54);
   if(!std::isfinite(steer)||!std::isfinite(throttle)||std::abs(steer)>.05f||std::abs(throttle)>.05f)manual=true;
  }
 }
 manual_[id]=manual;
 if(manual&&leases_.erase(id)){Put<std::uint32_t>(carrier,0x1d0,1);if(api_->log)api_->log("motion",CC2X_LOG_INFO,"event=MOTION_MANUAL_TAKEOVER carrier=%u",id);}
}
void Motion::BeforeMotion(void* carrier){
 if(!Context()||!Region(carrier,0xc98))return;auto id=At<std::uint32_t>(carrier,0x80);auto found=leases_.find(id);if(found==leases_.end())return;
 std::uint32_t team=0;
 if(tick_<found->second.tick||tick_-found->second.tick>3||!LocalTeam(&team)||team!=At<std::uint32_t>(carrier,0x108)||
    manual_[id]||api_->count(carrier,CC2X_VEHICLE_WAYPOINTS)==0){Stop(static_cast<unsigned char*>(carrier));leases_.erase(found);return;}
 Put<std::uint32_t>(carrier,0x1d0,0);
}
void Motion::AfterMotion(void* carrier){
 if(!Context()||!Region(carrier,0xc98))return;auto found=leases_.find(At<std::uint32_t>(carrier,0x80));if(found==leases_.end())return;
 for(unsigned off:{0xc30u,0xc34u}){auto* jet=At<unsigned char*>(carrier,off);if(!Region(jet,0x34,true))continue;
  float p=At<float>(jet,0x30);if(!std::isfinite(p))continue;
  const auto limited=std::copysign(std::min(std::abs(p),static_cast<float>(found->second.power)),p);Put<float>(jet,0x30,limited);
 }
}
void Motion::DropOwner(const std::string& owner){
 for(auto it=leases_.begin();it!=leases_.end();)if(it->second.owner==owner){if(auto* v=Carrier(it->first))Stop(v);it=leases_.erase(it);}else ++it;
}
bool Motion::Contacts(std::uint32_t id,std::vector<cc2x_contact>* out){
 auto* ship=Carrier(id);std::uint32_t local=0;if(!ship||!out||!LocalTeam(&local)||local!=At<std::uint32_t>(ship,0x108))return false;
 Ring teams;if(!RingAt(server_,0x504,4,64,&teams))return false;unsigned char* vision=nullptr;
 for(std::uint32_t i=0;i<teams.count;++i){auto* t=At<unsigned char*>(Item(teams,i,4),0);if(Region(t,0x140)&&At<std::uint32_t>(t,0x14)==local){if(vision)return false;vision=t;}}
 if(!vision)return false;Ring vehicles;if(!RingAt(vision,0x1c,0x80,8192,&vehicles))return false;
 std::vector<cc2x_contact> result;
 for(std::uint32_t i=0;i<vehicles.count;++i){auto* record=Item(vehicles,i,0x80);
  if(!At<unsigned char>(record,0x18)||At<unsigned char>(record,0x1b))continue;
  const auto vid=At<std::uint32_t>(record,0);auto* v=api_->find(CC2X_WORLD_VEHICLES,vid);if(!v)continue;
  cc2x_body b{};if(!api_->body_get(v,&b))continue;
  result.push_back({vid,CC2X_CONTACT_VEHICLE,static_cast<std::uint32_t>(api_->geti(v,CC2X_VEHICLE_DEFINITION,0)),
   static_cast<std::uint32_t>(api_->geti(v,CC2X_VEHICLE_TEAM,0)),b.pos[0],b.pos[1],b.pos[2]});
 }
 // Read missile metadata only after that team's own vision flag admits the ID.
 Ring seen,missiles;if(!RingAt(vision,0x2c,12,16384,&seen)||!RingAt(server_,0x3ec,4,16384,&missiles))return false;
 std::set<std::uint32_t> visible;
 for(std::uint32_t i=0;i<seen.count;++i){auto* r=Item(seen,i,12);if(At<unsigned char>(r,8)&&!At<unsigned char>(r,10))visible.insert(At<std::uint32_t>(r,0));}
 for(std::uint32_t i=0;i<missiles.count;++i){auto* m=At<unsigned char*>(Item(missiles,i,4),0);if(!Region(m,0x1d0)||!visible.count(At<std::uint32_t>(m,0)))continue;
  cc2x_contact c{At<std::uint32_t>(m,0),CC2X_CONTACT_PROJECTILE,At<std::uint32_t>(m,4),At<std::uint32_t>(m,8),
   At<double>(m,0x78),At<double>(m,0x80),At<double>(m,0x88)};
  if(c.definition<4096&&c.team<64&&std::isfinite(c.x)&&std::isfinite(c.y)&&std::isfinite(c.z))result.push_back(c);
 }
 if(result.size()>4096)return false;*out=std::move(result);return true;
}
}
