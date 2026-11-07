// SPDX-License-Identifier: GPL-3.0-or-later
#include "navigation.h"
#include "motion.h"
#include "navigation_geometry.h"
#include "platform/seh_guard.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
namespace cc2::api::carrier {
namespace {
template<class T>T At(const void* p,std::size_t o){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+o,sizeof(v));return v;}
template<class T>void Put(void* p,std::size_t o,T v){std::memcpy(static_cast<unsigned char*>(p)+o,&v,sizeof(v));}
bool Region(const void* p,std::size_t n){
 MEMORY_BASIC_INFORMATION m{};if(!p||!n||!VirtualQuery(p,&m,sizeof(m))||m.State!=MEM_COMMIT||(m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
 const auto a=reinterpret_cast<std::uintptr_t>(p),b=reinterpret_cast<std::uintptr_t>(m.BaseAddress);
 return a>=b&&a-b<=m.RegionSize&&n<=m.RegionSize-(a-b);
}
struct Ring {unsigned char* data;std::uint32_t cap,head,count;};
bool RingAt(const void* p,std::size_t off,std::size_t stride,Ring* out){
 if(!Region(static_cast<const unsigned char*>(p)+off,16))return false;
 const auto q=At<Ring>(p,off);
 if(q.count>q.cap||q.cap>65536||(q.cap&&(q.head>=q.cap||!Region(q.data,q.cap*stride)))||(!q.cap&&q.count))return false;
 *out=q;return true;
}
unsigned char* Item(const Ring& q,std::uint32_t i,std::size_t stride){return q.data+((q.head+i)%q.cap)*stride;}
using Predicate=int(__cdecl*)(const void*);
using Nearest=void*(__attribute__((thiscall))*)(void*,const float*,float*);
using Filtered=void*(__attribute__((thiscall))*)(void*,const float*,float*,Predicate);
using Ten=void(__attribute__((thiscall))*)(void*,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t);
using Project=void(__attribute__((thiscall))*)(void*,void*,void*,void*);
using Radius=float(__attribute__((thiscall))*)(void*,void*);
Navigation* installed=nullptr;
Ten original_follower=nullptr,original_helper=nullptr;
Project original_project=nullptr;
Filtered original_filtered=nullptr;
Nearest original_nearest=nullptr;
Radius original_radius=nullptr;
struct Scope {
 Navigation* owner=nullptr;void* carrier=nullptr;const cc2x_api* api=nullptr;
 std::uint64_t generation=0;const std::uint64_t* live_generation=nullptr;
 void* server=nullptr;void* const* live_server=nullptr;const DWORD* live_thread=nullptr;
 DWORD thread=0;cc2x_navigation_options options{};
 bool helper=false,blocked=false,cancelled=false,completed=false;
 void* mesh=nullptr;std::uint32_t height_key=0;
 Scope* previous_active=nullptr;
};
thread_local Scope* scope=nullptr;
thread_local Scope* active_scope=nullptr;
void Cancel(Navigation* owner,void* carrier=nullptr){
 for(auto* s=active_scope;s;s=s->previous_active)if(s->owner==owner&&(!carrier||s->carrier==carrier))s->cancelled=true;
}
bool Valid(const Scope* s){return s&&!s->cancelled&&s->live_generation&&s->live_server&&s->live_thread&&
 *s->live_generation==s->generation&&*s->live_server==s->server&&*s->live_thread==s->thread&&GetCurrentThreadId()==s->thread;}
Scope* Current(){return Valid(scope)?scope:nullptr;}
std::uint32_t Key(std::uint32_t bits){return navigation_geometry::OrderedKey(bits);}
bool FiniteBits(std::uint32_t bits){return (bits&0x7f800000u)!=0x7f800000u;}
bool Vertices(const void* mesh,const void* triangle,navigation_geometry::Vertex (&out)[3]){
 if(!Region(mesh,32)||!Region(triangle,0x38))return false;
 Ring vertices;if(!RingAt(mesh,0x10,0x10,&vertices)||!vertices.count)return false;
 for(unsigned i=0;i<3;++i){const auto index=At<std::uint32_t>(triangle,0x14+i*4);if(index>=vertices.count)return false;
  std::memcpy(&out[i],Item(vertices,index,0x10),sizeof(out[i]));}
 return true;
}
bool SetMesh(Scope& s,void* mesh){
 auto* island=static_cast<unsigned char*>(mesh)-0x50;
 if(!Region(island,0x100)||!Region(mesh,32))return false;
 const double y=At<double>(island,0xf0),limit=-s.options.minimum_depth-y;
 if(!std::isfinite(limit)||std::abs(limit)>1e7)return false;
 float f=static_cast<float>(limit);
 // A float vertex y < this ceiling is exactly y < the double threshold.
 if(static_cast<double>(f)<limit)f=std::nextafter(f,std::numeric_limits<float>::infinity());
 std::uint32_t bits{};std::memcpy(&bits,&f,4);s.mesh=mesh;s.height_key=Key(bits);return true;
}
// The hot callback performs only integer comparisons: preserve the engine's
// floating environment, reject nonfinite geometry, and never alter triangle flags.
bool Admitted(Scope& s,const void* triangle){
 navigation_geometry::Vertex v[3];if(!Vertices(s.mesh,triangle,v))return false;
 for(const auto& p:v){std::uint32_t b[3];std::memcpy(b,&p,sizeof(b));
  if(!FiniteBits(b[0])||!FiniteBits(b[1])||!FiniteBits(b[2])||Key(b[1])>=s.height_key)return false;}
 return true;
}
bool Goal(const cc2x_api* api,void* server,double x,double z,double depth,
 std::array<std::uint64_t,16>& stamp,const std::array<std::uint64_t,16>* previous){
 const auto count=api->count(nullptr,CC2X_WORLD_TILES);
 if(!server||api->server()!=server||count<=0||count>65536)return false;
 stamp[0]=reinterpret_cast<std::uintptr_t>(server);stamp[1]=static_cast<std::uint64_t>(count);
 auto* tile=static_cast<unsigned char*>(api->tile_at(x,z));
 stamp[2]=reinterpret_cast<std::uintptr_t>(tile);
 if(!tile)return true; // A live, nonempty native tile inventory admits open ocean.
 if(!Region(tile,0xf4))return false;
 auto* island=At<unsigned char*>(tile,0xf0);if(!Region(island,0x100))return false;
 const double ox=At<double>(island,0xe8),oy=At<double>(island,0xf0),oz=At<double>(island,0xf8);
 if(!std::isfinite(ox)||!std::isfinite(oy)||!std::isfinite(oz))return false;
 auto* mesh=island+0x50;Ring triangles;
 Ring vertices;if(!RingAt(mesh,0,4,&triangles)||!RingAt(mesh,0x10,0x10,&vertices))return false;
 stamp[3]=reinterpret_cast<std::uintptr_t>(island);stamp[4]=reinterpret_cast<std::uintptr_t>(triangles.data);
 stamp[5]=triangles.cap;stamp[6]=triangles.head;stamp[7]=triangles.count;
 stamp[8]=reinterpret_cast<std::uintptr_t>(vertices.data);stamp[9]=vertices.cap;stamp[10]=vertices.head;stamp[11]=vertices.count;
 stamp[12]=At<std::uint64_t>(island,0xe8);stamp[13]=At<std::uint64_t>(island,0xf0);stamp[14]=At<std::uint64_t>(island,0xf8);
 if(previous&&*previous==stamp)return true;
 for(std::uint32_t i=0;i<triangles.count;++i){auto* tri=At<unsigned char*>(Item(triangles,i,4),0);navigation_geometry::Vertex v[3];
  if(Vertices(mesh,tri,v)&&navigation_geometry::Wet(v,oy,depth)&&navigation_geometry::Contains(v,x-ox,z-oz))return true;}
 return false;
}
struct GoalCall {const cc2x_api* api;void* server;double x,z,depth;std::array<std::uint64_t,16> stamp{};const std::array<std::uint64_t,16>* previous=nullptr;bool admitted=false;};
void InvokeGoal(void* opaque){auto& c=*static_cast<GoalCall*>(opaque);c.admitted=Goal(c.api,c.server,c.x,c.z,c.depth,c.stamp,c.previous);}
void Forward(Ten fn,void* self,const std::uint32_t* a){fn(self,a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9]);}
struct Call {Ten fn;void* self;const std::uint32_t* args;};
void Invoke(void* opaque){auto& c=*static_cast<Call*>(opaque);Forward(c.fn,c.self,c.args);}
void __fastcall FollowerHook(void* self,void*,std::uint32_t a,std::uint32_t b,std::uint32_t c,std::uint32_t d,std::uint32_t e,std::uint32_t f,std::uint32_t g,std::uint32_t h,std::uint32_t i,std::uint32_t j){
 const std::uint32_t args[]{a,b,c,d,e,f,g,h,i,j};
 if(installed)installed->Follower(self,args);else Forward(original_follower,self,args);
}
void __fastcall HelperHook(void* self,void*,std::uint32_t a,std::uint32_t b,std::uint32_t c,std::uint32_t d,std::uint32_t e,std::uint32_t f,std::uint32_t g,std::uint32_t h,std::uint32_t i,std::uint32_t j){
 const std::uint32_t args[]{a,b,c,d,e,f,g,h,i,j};auto* s=Current();
 if(!s){Forward(original_helper,self,args);return;}
 if(s->carrier!=self){scope=nullptr;Call call{original_helper,self,args};const bool ok=cc2x_seh_guard(Invoke,&call)!=0;scope=Valid(s)?s:nullptr;if(!ok)s->blocked=true;return;}
 const bool previous=s->helper;s->helper=true;Forward(original_helper,self,args);s->helper=previous;
}
void __fastcall ProjectHook(void* self,void*,void* waypoint,void* tiles,void* sync){
 auto* s=Current();if(s&&s->carrier==self)return;original_project(self,waypoint,tiles,sync);
}
extern "C" int __cdecl cc2x_navigation_predicate_body(const void* triangle){auto* s=Current();return s&&s->helper&&s->mesh&&Admitted(*s,triangle);}
extern "C" __attribute__((naked,used)) int cc2x_navigation_predicate(const void*){
 __asm__("pushl %ebp\n\t" "movl %esp,%ebp\n\t" "andl $-16,%esp\n\t" "subl $512,%esp\n\t" "fxsave (%esp)\n\t"
 "subl $12,%esp\n\t" "pushl 8(%ebp)\n\t" "call _cc2x_navigation_predicate_body\n\t" "addl $16,%esp\n\t"
 "fxrstor (%esp)\n\t" "movl %ebp,%esp\n\t" "popl %ebp\n\t" "ret\n\t");
}
void* __fastcall FilteredHook(void* self,void*,const float* point,float* output,Predicate predicate){
 auto* s=Current();if(!s||!s->helper)return original_filtered(self,point,output,predicate);
 if(!SetMesh(*s,self)){s->blocked=true;return nullptr;}
 return original_filtered(self,point,output,cc2x_navigation_predicate);
}
void* __fastcall NearestHook(void* self,void*,const float* point,float* output){
 auto* s=Current();if(!s||!s->helper)return original_nearest(self,point,output);
 if(!SetMesh(*s,self)){s->blocked=true;return nullptr;}
 return original_filtered(self,point,output,cc2x_navigation_predicate);
}
float __fastcall RadiusHook(void* self,void*,void* point){auto* s=Current();return s&&s->carrier==self?static_cast<float>(s->options.arrival_radius):original_radius(self,point);}
extern "C" void __cdecl cc2x_navigation_mid_body(cc2x_cpu* cpu,void* opaque){
 auto* s=Current();if(!s)return;
 const auto site=reinterpret_cast<std::uintptr_t>(opaque);
 if(site==0){
  if(!s->helper||!s->mesh)return;
  const bool admitted=Admitted(*s,reinterpret_cast<const void*>(cpu->edx));
  cpu->eax=admitted?2:0;cpu->eflags=navigation_geometry::AndFlags(cpu->eflags,admitted);
  cpu->jump_to=static_cast<std::uint32_t>(s->api->va(0x5e2729));
 }else if(reinterpret_cast<void*>(cpu->edi)==s->carrier){
  const bool ready=At<unsigned char>(s->carrier,0x224)&&At<unsigned char>(s->carrier,0x225);
  if(site==2||!ready){if(site==2)s->blocked=true;cpu->jump_to=static_cast<std::uint32_t>(s->api->va(site==1?0x6c2540:0x6c256f));}
  else s->completed=true;
 }
}
extern "C" __attribute__((naked,used)) void cc2x_navigation_mid(cc2x_cpu*,void*){
 __asm__("pushl %ebp\n\t" "movl %esp,%ebp\n\t" "andl $-16,%esp\n\t" "subl $512,%esp\n\t" "fxsave (%esp)\n\t"
 "subl $8,%esp\n\t" "pushl 12(%ebp)\n\t" "pushl 8(%ebp)\n\t" "call _cc2x_navigation_mid_body\n\t" "addl $16,%esp\n\t"
 "fxrstor (%esp)\n\t" "movl %ebp,%esp\n\t" "popl %ebp\n\t" "ret\n\t");
}
struct Hook {std::uint32_t site,target;void* replacement;void** saved;};
void ExpectedCall(std::uint32_t site,std::uint32_t target,unsigned char (&bytes)[5]){bytes[0]=0xe8;const auto displacement=target-site-5;std::memcpy(bytes+1,&displacement,4);}
bool Bytes(const cc2x_api* api,std::uint32_t site,const unsigned char* bytes,std::size_t n){auto* p=reinterpret_cast<void*>(api->va(site));return Region(p,n)&&!std::memcmp(p,bytes,n);}
}
bool Navigation::Available()const{return installed_&&!faulted_&&motion_->Available();}
bool Navigation::Install(){
 if(installed_)return Available();const auto* a=motion_->api_;
 if(installed||!motion_->Available()||!a->tile_at||!a->server||!a->patch_call||!a->patch_mid||!a->patch_ptr||!a->patch_begin||!a->patch_abort||!a->patch_commit)return false;
 const Hook calls[]{
  {0x706d2c,0x6c2330,reinterpret_cast<void*>(FollowerHook),reinterpret_cast<void**>(&original_follower)},
  {0x6c23cc,0x6c7770,reinterpret_cast<void*>(ProjectHook),reinterpret_cast<void**>(&original_project)},
  {0x6c2979,0x6c7920,reinterpret_cast<void*>(HelperHook),reinterpret_cast<void**>(&original_helper)},
  {0x5e21bb,0x5e1780,reinterpret_cast<void*>(FilteredHook),reinterpret_cast<void**>(&original_filtered)},
  {0x5e21df,0x5e15f0,reinterpret_cast<void*>(NearestHook),reinterpret_cast<void**>(&original_nearest)}};
 constexpr unsigned char gate[]{0x8b,0x42,0x30,0x83,0xe0,2};
 constexpr unsigned char reached[]{0xc6,0x87,0x26,2,0,0,1};
 constexpr unsigned char ret10[]{0xc2,0x28,0},ret3[]{0xc2,0x0c,0},ret1[]{0xc2,4,0};
 for(const auto& h:calls){unsigned char b[5];ExpectedCall(h.site,h.target,b);if(!Bytes(a,h.site,b,5))return false;}
 if(!Bytes(a,0x5e2723,gate,sizeof(gate))||!Bytes(a,0x6c2539,reached,sizeof(reached))||!Bytes(a,0x6c2568,reached,sizeof(reached))||
 !Bytes(a,0x6c4420,ret10,sizeof(ret10))||!Bytes(a,0x6c824a,ret10,sizeof(ret10))||!Bytes(a,0x6c791d,ret3,sizeof(ret3))||!Bytes(a,0x708296,ret1,sizeof(ret1)))return false;
 // Verify argument construction, return cleanup and the untouched gate branch.
 constexpr unsigned char abi0[]{0xc2,0x08,0x00};
 constexpr unsigned char abi1[]{0xc2,0x08,0x00};
 constexpr unsigned char abi2[]{0xc2,0x0c,0x00};
 constexpr unsigned char abi3[]{0xc2,0x0c,0x00};
 constexpr unsigned char abi4[]{0x68,0x10,0x22,0x5e,0x00,0x50,0x51,0x8b,0xce,0xe8,0xc0,0xf5,0xff,0xff,0xeb,0x10,0x8b,0x75,0x08,0x8d,0x44,0x24,0x0c,0x50,0x51,0x8b,0xce,0xe8,0x1e,0xf4,0xff,0xff,0x8b,0xf8,0x8b,0xce,0x8d,0x44,0x24,0x0c,0x50,0xff,0x74,0x24,0x0c,0xe8,0x0c,0xf4,0xff,0xff,0x85,0xff,0x74,0x20,0x85,0xc0,0x74,0x1c,0xff,0x75,0x14,0x8b,0xd0,0x8b,0xcf,0xff,0x75,0x10,0xff,0x75,0x0c,0x56,0xe8,0x31,0x00,0x00,0x00,0x83,0xc4,0x10,0x5f,0x5e,0x8b,0xe5,0x5d,0xc3,0x5f,0x32,0xc0,0x5e,0x8b,0xe5,0x5d,0xc3};
 constexpr unsigned char abi5[]{0x75,0x2e,0xc6,0x42,0x10,0x01};
 constexpr unsigned char abi6[]{0x56,0xff,0x73,0x0c,0x0f,0x42,0xd1,0x8b,0xcf,0xc1,0xe2,0x06,0x03,0x97,0x40,0x01,0x00,0x00,0x52,0x89,0x55,0xec,0xe8,0x9f,0x53,0x00,0x00,0x8b,0x45,0xec,0x8b,0x55,0xd0};
 constexpr unsigned char abi7[]{0x0f,0x10,0x06,0x56,0xff,0x73,0x0c,0x51,0xff,0x73,0x10,0x8b,0xcf,0x83,0xec,0x18,0x8b,0xc4,0x0f,0x11,0x00,0xf3,0x0f,0x7e,0x46,0x10,0x66,0x0f,0xd6,0x40,0x10,0xe8,0xa2,0x4f,0x00,0x00,0xe9,0x8a,0x1a,0x00,0x00};
 constexpr unsigned char abi8[]{0x8d,0x44,0x24,0x48,0xc6,0x87,0x50,0x03,0x00,0x00,0x01,0x50,0x8d,0x87,0xf8,0x01,0x00,0x00,0xf2,0x0f,0x11,0x44,0x24,0x5c,0x50,0x8d,0x87,0xf0,0x01,0x00,0x00,0xc7,0x44,0x24,0x24,0x00,0x00,0x00,0x00,0x50,0x8d,0x44,0x24,0x28,0x50,0x8d,0x8f,0xd8,0x01,0x00,0x00,0x51,0xff,0x75,0x18,0x8b,0xcf,0xff,0x75,0x1c,0x0f,0x11,0x44,0x24,0x64,0xff,0x75,0x14,0xff,0x75,0x0c,0xff,0x75,0x10,0xe8,0xff,0xb5,0xfb,0xff,0x8b,0x87,0x94,0x00,0x00,0x00};
 constexpr unsigned char abi9[]{0xd9,0x05,0x40,0x7a,0xdb,0x00,0xc2,0x04,0x00};
 if(!Bytes(a,0x5e173e,abi0,sizeof(abi0)))return false;
 if(!Bytes(a,0x5e176e,abi1,sizeof(abi1)))return false;
 if(!Bytes(a,0x5e18ca,abi2,sizeof(abi2)))return false;
 if(!Bytes(a,0x5e18f6,abi3,sizeof(abi3)))return false;
 if(!Bytes(a,0x5e21b2,abi4,sizeof(abi4)))return false;
 if(!Bytes(a,0x5e2729,abi5,sizeof(abi5)))return false;
 if(!Bytes(a,0x6c23b6,abi6,sizeof(abi6)))return false;
 if(!Bytes(a,0x6c295a,abi7,sizeof(abi7)))return false;
 if(!Bytes(a,0x706ce2,abi8,sizeof(abi8)))return false;
 if(!Bytes(a,0x708290,abi9,sizeof(abi9)))return false;
 a->patch_begin("cc2x.navigation.surface");
 for(const auto& h:calls){unsigned char b[5];ExpectedCall(h.site,h.target,b);if(!a->patch_call(h.site,5,b,h.replacement,h.saved)){a->patch_abort();return false;}}
 if(!a->patch_mid(0x5e2723,sizeof(gate),gate,cc2x_navigation_mid,nullptr)||
 !a->patch_mid(0x6c2539,sizeof(reached),reached,cc2x_navigation_mid,reinterpret_cast<void*>(1))||
 !a->patch_mid(0x6c2568,sizeof(reached),reached,cc2x_navigation_mid,reinterpret_cast<void*>(2))||
 !a->patch_ptr(0xd596ec+0x34,0x708290,reinterpret_cast<void*>(RadiusHook),reinterpret_cast<void**>(&original_radius))){a->patch_abort();return false;}
 if(!a->patch_commit()){a->patch_abort();return false;}installed=this;installed_=true;return true;
}
void Navigation::Session(){Cancel(this);if(scope&&scope->owner==this)scope=nullptr;policies_.clear();}
void Navigation::Invalidate(std::uint32_t id,const Policy& policy){
 auto* m=motion_;if(!m->Context()||!m->api_->server||m->api_->server()!=m->server_)return;
 auto* v=m->Carrier(id);std::uint32_t team=0;if(v!=policy.carrier||!v||!m->LocalTeam(&team)||team!=m->api_->geti(v,CC2X_VEHICLE_TEAM,0))return;
 // Native cache invalidation clears +224 and discards these two rings. Keep
 // their storage under native ownership while making every cached node empty.
 Put<unsigned char>(v,0x224,0);Put<unsigned char>(v,0x225,0);Put<unsigned char>(v,0x226,0);Put<unsigned char>(v,0x227,1);
 for(const auto off:{0x25cu,0x26cu}){Ring ring;if(RingAt(v,off,off==0x25c?4:12,&ring)){Put<std::uint32_t>(v,off+8,0);Put<std::uint32_t>(v,off+12,0);}}
}
void Navigation::Tick(){
 for(auto it=policies_.begin();it!=policies_.end();){auto lease=motion_->leases_.find(it->first);
  if((lease==motion_->leases_.end()||lease->second.owner!=it->second.owner)&&
    (motion_->tick_<it->second.last_tick||motion_->tick_-it->second.last_tick>3)){
   Invalidate(it->first,it->second);it=policies_.erase(it);
  }else ++it;
 }
}
void Navigation::Drop(const std::string& owner,std::uint32_t id){for(auto it=policies_.begin();it!=policies_.end();)if((!id||it->first==id)&&it->second.owner==owner){Cancel(this,it->second.carrier);Invalidate(it->first,it->second);it=policies_.erase(it);}else ++it;}
void Navigation::Drop(std::uint32_t id){auto it=policies_.find(id);if(it!=policies_.end())Drop(it->second.owner,id);}
bool Navigation::AllowsOwner(const std::string& owner,std::uint32_t id)const{auto it=policies_.find(id);return it==policies_.end()||it->second.owner==owner;}
bool Navigation::Blocked(std::uint32_t id)const{auto it=policies_.find(id);return it!=policies_.end()&&(it->second.state.flags&(CC2X_NAVIGATION_BLOCKED|CC2X_NAVIGATION_FAULTED));}
bool Navigation::Arrived(std::uint32_t id)const{auto it=policies_.find(id);return it!=policies_.end()&&(it->second.state.flags&CC2X_NAVIGATION_ARRIVED);}
bool Navigation::Configure(const std::string& owner,std::uint64_t generation,std::uint32_t id,const cc2x_navigation_options& o){
 auto* m=motion_;if(!m->Context()||!m->api_->server||m->api_->server()!=m->server_||generation!=m->generation_||owner.empty()||owner.size()>96||o.size!=sizeof(o)||o.flags>CC2X_NAVIGATION_SURFACE)return false;
 auto* v=m->Carrier(id);std::uint32_t team=0;if(!v||!m->LocalTeam(&team)||team!=m->api_->geti(v,CC2X_VEHICLE_TEAM,0)||!AllowsOwner(owner,id))return false;
 const auto lease=m->leases_.find(id);if(lease!=m->leases_.end()&&lease->second.owner!=owner)return false;
 if(!std::isfinite(o.minimum_depth)||o.minimum_depth<0||o.minimum_depth>1000||!std::isfinite(o.arrival_radius)||o.arrival_radius<1||o.arrival_radius>150)return false;
 if(!o.flags){if(!Available()&&!policies_.count(id))return false;Drop(owner,id);return true;}
 if(!Available()||Current()||m->manual_[id])return false;
 auto it=policies_.find(id);if(it==policies_.end()&&policies_.size()>=8)return false;
 auto& p=policies_[id];if(p.owner.empty()||p.options.minimum_depth!=o.minimum_depth||p.options.arrival_radius!=o.arrival_radius){if(!p.owner.empty()){Cancel(this,p.carrier);Invalidate(id,p);}p.dirty=true;p.state={sizeof(p.state),id,CC2X_NAVIGATION_CONFIGURED,0,0,0,0,o.minimum_depth,o.arrival_radius};}
 p.owner=owner;p.options=o;p.carrier=v;p.last_tick=m->tick_;return true;
}
bool Navigation::Read(std::uint32_t id,cc2x_navigation_state* out){
 auto* m=motion_;if(!out||out->size!=sizeof(*out)||!m->Context()||!m->Carrier(id))return false;
 std::uint32_t team=0;if(!m->LocalTeam(&team)||team!=m->api_->geti(m->Carrier(id),CC2X_VEHICLE_TEAM,0))return false;
 cc2x_navigation_state value{sizeof(value),id};auto it=policies_.find(id);
 if(it!=policies_.end()){value=it->second.state;value.flags|=CC2X_NAVIGATION_CONFIGURED;auto lease=m->leases_.find(id);if(lease!=m->leases_.end()&&lease->second.owner==it->second.owner)value.flags|=CC2X_NAVIGATION_ACTIVE;}
 if(faulted_)value.flags|=CC2X_NAVIGATION_FAULTED;*out=value;return true;
}
void Navigation::Follower(void* self,const std::uint32_t args[10]){
 auto* previous=scope;scope=nullptr;auto* m=motion_;
 const auto id=Region(self,0xe7c)?At<std::uint32_t>(self,0x80):0;
 auto policy=policies_.find(id);auto lease=m->leases_.find(id);
 if(faulted_&&policy!=policies_.end()&&lease!=m->leases_.end()&&lease->second.owner==policy->second.owner){policy->second.state.flags|=CC2X_NAVIGATION_BLOCKED|CC2X_NAVIGATION_FAULTED;if(m->Context()&&m->Carrier(id)==self&&policy->second.carrier==self)m->Stop(static_cast<unsigned char*>(self));scope=Valid(previous)?previous:nullptr;return;}
 const bool owned=policy!=policies_.end()&&lease!=m->leases_.end()&&lease->second.owner==policy->second.owner;
 if(owned&&(!Available()||!m->Context()||m->Carrier(id)!=self||policy->second.carrier!=self||!m->api_->server||m->api_->server()!=m->server_)){policy->second.state.flags|=CC2X_NAVIGATION_BLOCKED;scope=Valid(previous)?previous:nullptr;return;}
 if(!owned){
  if(previous){Call c{original_follower,self,args};const bool ok=cc2x_seh_guard(Invoke,&c)!=0;scope=Valid(previous)?previous:nullptr;if(!ok)previous->blocked=true;}
  else Forward(original_follower,self,args);return;
 }
 auto& p=policy->second;Ring waypoints;cc2x_body body{};
 const bool valid=RingAt(self,0x140,0x40,&waypoints)&&waypoints.count&&m->api_->body_get(self,&body);
 if(!valid){p.state.flags=CC2X_NAVIGATION_CONFIGURED|CC2X_NAVIGATION_BLOCKED;m->Stop(static_cast<unsigned char*>(self));scope=Valid(previous)?previous:nullptr;return;}
 auto* waypoint=Item(waypoints,0,0x40);const auto waypoint_id=At<std::uint32_t>(waypoint,0);
 const double x=At<double>(waypoint,8),z=At<double>(waypoint,0x10);
 if(p.state.waypoint_id!=waypoint_id||p.state.target_x!=x||p.state.target_z!=z)p.dirty=true;
 p.state.waypoint_id=waypoint_id;p.state.target_x=x;p.state.target_z=z;p.state.distance=std::hypot(x-body.pos[0],z-body.pos[2]);
 GoalCall goal{m->api_,m->server_,x,z,p.options.minimum_depth};
 // Native terrain geometry is stable between reloads. Recheck its identity on
 // every call; periodically rescan as well to bound unannounced in-place changes.
 if(!p.dirty&&p.goal_admitted&&m->tick_>=p.admitted_tick&&m->tick_-p.admitted_tick<300)goal.previous=&p.geometry;
 const bool valid_target=std::isfinite(p.state.distance)&&At<std::uint32_t>(waypoint,4)==0;
 const bool checked=valid_target&&cc2x_seh_guard(InvokeGoal,&goal);
 if(!checked||!goal.admitted)p.state.flags|=CC2X_NAVIGATION_BLOCKED;
 if(valid_target&&!checked){faulted_=true;p.state.flags|=CC2X_NAVIGATION_FAULTED;}
 if(checked&&goal.admitted){p.goal_admitted=true;p.geometry=goal.stamp;if(!goal.previous)p.admitted_tick=m->tick_;}
 if(p.state.flags&(CC2X_NAVIGATION_BLOCKED|CC2X_NAVIGATION_FAULTED)){m->Stop(static_cast<unsigned char*>(self));scope=Valid(previous)?previous:nullptr;return;}
 if(p.dirty){Put<unsigned char>(self,0x224,0);Put<unsigned char>(self,0x225,0);Put<unsigned char>(self,0x226,0);p.dirty=false;}
 const auto owner=p.owner;const auto* identity=&p;
 Scope current{this,self,m->api_,m->generation_,&m->generation_,m->server_,&m->server_,&m->thread_,GetCurrentThreadId(),p.options};
 current.previous_active=active_scope;active_scope=&current;
 scope=&current;Call call{original_follower,self,args};bool ok=false;
 try{ok=cc2x_seh_guard(Invoke,&call)!=0;}catch(...){ok=false;}
 active_scope=current.previous_active;scope=Valid(previous)?previous:nullptr;
 // Fault-disable survives cancellation; a result belongs only to the exact
 // still-live invocation, lease and policy that entered native code. An active
 // invocation list also catches cancellation while a nested foreign call has
 // temporarily cleared the admission scope.
 if(!ok)faulted_=true;
 if(!Valid(&current)||!m->Context()||m->api_!=current.api||m->Carrier(id)!=self||!m->api_->server||m->api_->server()!=current.server)return;
 policy=policies_.find(id);if(policy==policies_.end())return;
 lease=m->leases_.find(id);
 if(&policy->second!=identity||policy->second.owner!=owner||policy->second.carrier!=self||
    policy->second.options.flags!=current.options.flags||policy->second.options.minimum_depth!=current.options.minimum_depth||
    policy->second.options.arrival_radius!=current.options.arrival_radius||lease==m->leases_.end()||lease->second.owner!=owner)return;
 auto& result=policy->second;result.state.flags=CC2X_NAVIGATION_CONFIGURED;
 if(faulted_)result.state.flags|=CC2X_NAVIGATION_FAULTED;
 const bool ready=(At<unsigned char>(self,0x224)&&At<unsigned char>(self,0x225))||current.completed;
 if(current.blocked||!ready||faulted_){result.state.flags|=CC2X_NAVIGATION_BLOCKED;m->Stop(static_cast<unsigned char*>(self));}
 else{result.state.flags|=CC2X_NAVIGATION_READY;if(current.completed)result.dirty=true;if(!m->api_->count(self,CC2X_VEHICLE_WAYPOINTS)&&current.completed&&result.state.distance<=result.options.arrival_radius){result.state.flags|=CC2X_NAVIGATION_ARRIVED;result.last_tick=m->tick_;}}
}
}
