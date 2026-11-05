// SPDX-License-Identifier: GPL-3.0-or-later
// Checked engine primitives derived from Yolo / totolerigolo118's Brake v15,
// a40d1572eabd6d9710e6d64aae431824ffc98790, tour/atterrissage_manuel/frein.
// No brake policy, auto-recall timer, power writes or vehicle relocation here.
#include "recovery.h"
#include "platform/seh_guard.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>
namespace cc2::api::carrier {
namespace {
constexpr RecoveryProfile profiles[]{
 {"1.5.18-win32",0x6a36c177,0x7018ce,0x701a3d,0x701a8a,0x6fe470,0x6ff20a,0x6fdcdd,0x6fdcf2,0xd596cc,0x707330},
 {"1.5.18-win32-r2",0x6ae228f3,0x7019de,0x701b4d,0x701b9a,0x6fe580,0x6ff31a,0x6fdded,0x6fde02,0xd596ec,0x707440}
};
constexpr unsigned char h1[]{0x8b,0x40,0x04,0x83,0xf8,0x05},h2[]{0x8d,0x85,0x28,0xfd,0xff,0xff};
constexpr unsigned char h5[]{0xc6,0x85,0xfb,0xfd,0xff,0xff,0x01},h6[]{0xf3,0x0f,0x59,0xd2,0xf3,0x0f,0x59,0xdb};
constexpr unsigned char dockBytes[]{0x55,0x8b,0xec,0x51,0x56,0x57,0xff,0x75,0x0c,0x8b,0xf1,0x8b,0x4d,0x08};
constexpr unsigned char h2next[]{0xff,0xb5,0xfc,0xfd,0xff,0xff,0x8d,0x4f,0x34};
constexpr unsigned char h5next[]{0xf3,0x0f,0x10,0x95,0x1c,0xfd,0xff,0xff,0xf3,0x0f,0x10,0x85,0xcc,0xfd,0xff,0xff};
constexpr unsigned char h6next[]{0x8b,0x8d,0x88,0xfd,0xff,0xff,0x03,0x4f,0x3c};
bool Region(const void* p,std::size_t n){
 MEMORY_BASIC_INFORMATION m{};if(!p||!n||!VirtualQuery(p,&m,sizeof(m))||m.State!=MEM_COMMIT||(m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
 auto a=reinterpret_cast<std::uintptr_t>(p),b=reinterpret_cast<std::uintptr_t>(m.BaseAddress);
 if(a<b||a-b>m.RegionSize||n>m.RegionSize-(a-b))return false;
 const auto access=m.Protect&0xff;
 return access==PAGE_READONLY||access==PAGE_READWRITE||access==PAGE_WRITECOPY||access==PAGE_EXECUTE_READ||access==PAGE_EXECUTE_READWRITE||access==PAGE_EXECUTE_WRITECOPY;
}
template<class T>T At(const void* p,std::size_t o){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+o,sizeof(v));return v;}
template<std::size_t N>bool Bytes(const cc2x_api* a,std::uint32_t address,const unsigned char(&bytes)[N]){
 auto* p=reinterpret_cast<const void*>(a->va(address));return Region(p,N)&&std::memcmp(p,bytes,N)==0;
}
bool Owner(const std::string& name){
 if(name.size()<5||name.size()>96||(name.compare(0,4,"lua.")&&name.compare(0,7,"native.")))return false;
 for(char c:name)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return false;
 return true;
}
void Dock(std::uintptr_t fn,void* ship,void* world,std::uint32_t id){
 using Fn=void(__attribute__((thiscall))*)(void*,void*,std::uint32_t);reinterpret_cast<Fn>(fn)(ship,world,id);
}
}
extern "C" __attribute__((used,noinline)) void cc2x_recovery_body(cc2x_cpu* cpu,void* opaque){
 auto value=reinterpret_cast<std::uintptr_t>(opaque);auto* r=reinterpret_cast<Recovery*>(value&~std::uintptr_t(3));
 r->Hook(static_cast<unsigned>(value&3),cpu);
}
// Existing patch_mid preserves GPRs/flags, not live SSE values. Save all eight
// XMM registers around every C++/VirtualQuery call, as required at these sites.
extern "C" __attribute__((naked,used)) void cc2x_recovery_hook(cc2x_cpu*,void*){
 __asm__("subl $0x80, %esp\n\t"
 "movups %xmm0, 0x00(%esp)\n\t" "movups %xmm1, 0x10(%esp)\n\t" "movups %xmm2, 0x20(%esp)\n\t" "movups %xmm3, 0x30(%esp)\n\t"
 "movups %xmm4, 0x40(%esp)\n\t" "movups %xmm5, 0x50(%esp)\n\t" "movups %xmm6, 0x60(%esp)\n\t" "movups %xmm7, 0x70(%esp)\n\t"
 "pushl 0x88(%esp)\n\t" "pushl 0x88(%esp)\n\t" "call _cc2x_recovery_body\n\t" "addl $8, %esp\n\t"
 "movups 0x00(%esp), %xmm0\n\t" "movups 0x10(%esp), %xmm1\n\t" "movups 0x20(%esp), %xmm2\n\t" "movups 0x30(%esp), %xmm3\n\t"
 "movups 0x40(%esp), %xmm4\n\t" "movups 0x50(%esp), %xmm5\n\t" "movups 0x60(%esp), %xmm6\n\t" "movups 0x70(%esp), %xmm7\n\t"
 "addl $0x80, %esp\n\t" "ret\n\t");
}
void Recovery::Configure(const cc2x_api* api){
 std::lock_guard<std::recursive_mutex> l(mutex_);api_=api;profile_=nullptr;
 if(api&&api->game_build)for(const auto& p:profiles)if(std::strcmp(api->game_build,p.name)==0)profile_=&p;
}
bool Recovery::Context() const{return api_&&server_&&thread_==GetCurrentThreadId()&&api_->server&&api_->server()==server_;}
bool Recovery::Profile(bool hooks) const{
 if(!api_||!profile_||!api_->va||!api_->find||!api_->geti||!api_->getf||!api_->count||!api_->body_get)return false;
 auto* d=reinterpret_cast<const IMAGE_DOS_HEADER*>(api_->module_base);
 if(!Region(d,sizeof(*d))||d->e_magic!=IMAGE_DOS_SIGNATURE||d->e_lfanew<sizeof(*d)||d->e_lfanew>0x100000)return false;
 auto* n=reinterpret_cast<const IMAGE_NT_HEADERS32*>(api_->module_base+d->e_lfanew);
 if(!Region(n,sizeof(*n))||n->Signature!=IMAGE_NT_SIGNATURE||n->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||
 n->FileHeader.TimeDateStamp!=profile_->timestamp||n->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC||n->OptionalHeader.SizeOfImage!=0xaf7000)return false;
 if(!Bytes(api_,profile_->dock,dockBytes))return false;
 const auto vt=api_->va(profile_->vtable);
 if(!Region(reinterpret_cast<void*>(vt),0x80)||At<std::uint32_t>(reinterpret_cast<void*>(vt),0x7c)!=api_->va(profile_->dock))return false;
 if(!hooks)return true;
 if(!api_->patch_begin||!api_->patch_mid||!api_->patch_commit||!api_->patch_abort)return false;
 return (installed_||(Bytes(api_,profile_->h1,h1)&&Bytes(api_,profile_->h2,h2)&&Bytes(api_,profile_->h5,h5)&&Bytes(api_,profile_->h6,h6)))&&
 Bytes(api_,profile_->h2next,h2next)&&Bytes(api_,profile_->h5next,h5next)&&Bytes(api_,profile_->h6next,h6next);
}
bool Recovery::Available(std::uint32_t capability){
 std::lock_guard<std::recursive_mutex> l(mutex_);
 return !faulted_&&(capability==CC2X_CARRIER_DOCK||capability==CC2X_CARRIER_RECOVERY)&&Profile(capability==CC2X_CARRIER_RECOVERY);
}
bool Recovery::Install(){
 if(installed_)return true;if(!Profile(true))return false;
 api_->patch_begin("cc2x.carrier_recovery");
 const std::uint32_t sites[]{profile_->h1,profile_->h2,profile_->h5,profile_->h6};
 const unsigned char* data[]{h1,h2,h5,h6};const unsigned sizes[]{sizeof(h1),sizeof(h2),sizeof(h5),sizeof(h6)};
 static_assert(alignof(Recovery)>=4);
 for(unsigned i=0;i<4;++i)if(!api_->patch_mid(sites[i],sizes[i],data[i],cc2x_recovery_hook,
 reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(this)|i))){api_->patch_abort();return false;}
 if(!api_->patch_commit()){api_->patch_abort();return false;}
 installed_=true;return true;
}
void Recovery::Session(void* server,std::uint64_t generation){
 std::lock_guard<std::recursive_mutex> l(mutex_);server_=server;generation_=generation;thread_=0;tick_=0;
 requests_.clear();used_.clear();owner_.clear();status_={sizeof(status_)};ours_=false;manager_=nullptr;
 // An uncertain native call remains unavailable for the process lifetime.
}
void Recovery::Stop(std::uint32_t reason){status_.armed=0;status_.reason=reason;status_.remaining_ticks=0;ours_=false;manager_=nullptr;}
bool Recovery::Pair(std::uint32_t carrier,std::uint32_t vehicle,void** ship,void** aircraft) const{
 if(!carrier||!vehicle||carrier==vehicle)return false;
 *ship=api_->find(CC2X_WORLD_VEHICLES,carrier);*aircraft=api_->find(CC2X_WORLD_VEHICLES,vehicle);
 return *ship&&*aircraft&&api_->geti(*ship,CC2X_VEHICLE_DEFINITION,0)==0&&api_->geti(*aircraft,CC2X_VEHICLE_IS_AIR,0)&&
 api_->geti(*ship,CC2X_VEHICLE_TEAM,0)==api_->geti(*aircraft,CC2X_VEHICLE_TEAM,0);
}
bool Recovery::FreeBay(const unsigned char* ship) const{
 if(!Region(ship,0x160))return false;
 auto* bays=At<const unsigned char*>(ship,0x154);auto count=At<std::uint32_t>(ship,0x158);
 auto* model=At<const unsigned char*>(ship,0x88);
 if(!count||count>64||!Region(bays,count*0x70)||!Region(model,0xf0))return false;
 auto* nodes=At<const unsigned char*>(model,0xe0);auto n=At<std::uint32_t>(model,0xe4),first=At<std::uint32_t>(model,0xe8);
 if(!n||n>4096||first>=n||count>n||!Region(nodes,n*64))return false;
 for(std::uint32_t i=0;i<count;++i)if(!At<std::uint32_t>(bays+i*0x70,0x64)&&At<std::uint32_t>(nodes+((first+i)%n)*64,0x3c)==1)return true;
 return false;
}
bool Recovery::Parked(void* ship,void* aircraft) const{
 if(api_->geti(aircraft,CC2X_VEHICLE_ATTACHED_TO,0)||api_->geti(aircraft,CC2X_VEHICLE_CONTROL_MODE,0)==1)return false;
 const auto count=api_->count(aircraft,CC2X_VEHICLE_WHEELS);if(count<1||count>8)return false;
 for(int i=0;i<count;++i)if(!(api_->getf(aircraft,CC2X_VEHICLE_WHEEL_CONTACT,i)>=0.01))return false;
 cc2x_body s{},a{};if(!api_->body_get(ship,&s)||!api_->body_get(aircraft,&a))return false;
 double distance=0,speed=0;
 for(int i=0;i<3;++i){if(!std::isfinite(a.pos[i])||!std::isfinite(s.pos[i])||!std::isfinite(a.vel[i])||!std::isfinite(s.vel[i]))return false;
 const double d=a.pos[i]-s.pos[i],v=a.vel[i]-s.vel[i];distance+=d*d;speed+=v*v;}
 return distance<=300.0*300.0&&std::abs(a.pos[1]-s.pos[1])<=50&&speed<=1.0&&FreeBay(static_cast<unsigned char*>(ship));
}
bool Recovery::LiveWindow(){
 if(!status_.armed||!Context())return false;
 if(tick_>=deadline_){Stop(CC2X_RECOVERY_EXPIRED);return false;}
 void* s=nullptr;void* a=nullptr;
 if(!Pair(status_.carrier_id,status_.vehicle_id,&s,&a)){Stop(CC2X_RECOVERY_INVALIDATED);return false;}
 const auto dock=api_->geti(a,CC2X_VEHICLE_DOCK_STATE,0);
 if(dock!=5&&dock!=1){Stop(dock==6||dock==4?CC2X_RECOVERY_TAKEN_OVER:CC2X_RECOVERY_INVALIDATED);return false;}
 if(api_->geti(a,CC2X_VEHICLE_DOCK_VEHICLE,0)!=status_.carrier_id||!Parked(s,a)){Stop(CC2X_RECOVERY_INVALIDATED);return false;}
 return true;
}
std::uint64_t Recovery::Request(const std::string& owner,std::uint64_t generation,std::uint32_t carrier,std::uint32_t vehicle){
 std::lock_guard<std::recursive_mutex> l(mutex_);void* s=nullptr;void* a=nullptr;
 if(!Owner(owner)||!Context()||generation!=generation_||!Available(CC2X_CARRIER_DOCK)||!Pair(carrier,vehicle,&s,&a)||next_>=INT64_MAX)return 0;
 unsigned pending=0,own=0;for(const auto& r:requests_)if(r.result.code==CC2X_DOCK_PENDING){
 ++pending;if(r.owner==owner)++own;if(r.result.vehicle_id==vehicle)return 0;}
 if(pending>=16||own>=4)return 0;
 if(requests_.size()>=64){auto it=std::find_if(requests_.begin(),requests_.end(),[](const auto& r){return r.result.code!=CC2X_DOCK_PENDING;});
 if(it==requests_.end())return 0;requests_.erase(it);}
 const auto id=++next_;requests_.push_back({owner,{sizeof(cc2x_dock_result),carrier,vehicle,CC2X_DOCK_PENDING,id,generation_},tick_});return id;
}
bool Recovery::Result(const std::string& owner,std::uint64_t request,cc2x_dock_result* out){
 std::lock_guard<std::recursive_mutex> l(mutex_);if(!Context()||!out||out->size!=sizeof(*out))return false;
 for(const auto& r:requests_)if(r.owner==owner&&r.result.request_id==request){*out=r.result;return true;}return false;
}
void Recovery::Tick(void* server,std::uint64_t generation,std::uint64_t tick){
 std::lock_guard<std::recursive_mutex> l(mutex_);if(!server||server!=server_||generation!=generation_||tick<tick_)return;
 if(thread_&&thread_!=GetCurrentThreadId())return;thread_=GetCurrentThreadId();tick_=tick;
 if(!Context())return;
 for(auto it=used_.begin();it!=used_.end();){auto* a=api_->find(CC2X_WORLD_VEHICLES,it->first);
 const auto dock=a?api_->geti(a,CC2X_VEHICLE_DOCK_STATE,0):0;
 if(!a||(dock!=5&&dock!=1)||api_->geti(a,CC2X_VEHICLE_DOCK_VEHICLE,0)!=it->second)it=used_.erase(it);else ++it;}
 if(status_.armed)LiveWindow();
 // Do not retain deque iterators/references across a native engine call: a
 // synchronous event can enqueue, remove an owner, or end the session.
 std::vector<RequestRecord> ready;
 for(const auto& r:requests_)if(r.result.code==CC2X_DOCK_PENDING&&r.tick<tick_)ready.push_back(r);
 for(const auto& r:ready){
 if(!Context()||r.result.generation!=generation_)break;
 auto present=[&](const auto& item){return item.result.request_id==r.result.request_id&&item.owner==r.owner;};
 if(std::find_if(requests_.begin(),requests_.end(),present)==requests_.end())continue;
 const auto code=Execute(r.result);
 auto found=std::find_if(requests_.begin(),requests_.end(),present);
 if(r.result.generation!=generation_||found==requests_.end())continue;
 found->result.code=code;
 if(api_->log)api_->log("carrier",CC2X_LOG_INFO,"event=DOCK_REQUEST_RESULT request=%llu carrier=%u vehicle=%u code=%u",r.result.request_id,r.result.carrier_id,r.result.vehicle_id,code);
 }
}
std::uint32_t Recovery::Execute(const cc2x_dock_result& result){
 void* ship=nullptr;void* aircraft=nullptr;
 if(!Context()||!Available(CC2X_CARRIER_DOCK))return CC2X_DOCK_PROFILE_REFUSED;
 if(!Pair(result.carrier_id,result.vehicle_id,&ship,&aircraft))return CC2X_DOCK_INVALID_VEHICLE;
 auto dock=api_->geti(aircraft,CC2X_VEHICLE_DOCK_STATE,0);
 if((dock==5||dock==1)&&api_->geti(aircraft,CC2X_VEHICLE_DOCK_VEHICLE,0)==result.carrier_id)return CC2X_DOCK_ALREADY_QUEUED;
 if(dock!=0||api_->geti(aircraft,CC2X_VEHICLE_ATTACHED_TO,0))return CC2X_DOCK_ENGINE_REFUSED;
 if(!Region(ship,4)||At<std::uint32_t>(ship,0)!=api_->va(profile_->vtable)||!Region(server_,0x354)){
 return CC2X_DOCK_PROFILE_REFUSED;}
 struct Call {DockInvoker fn;std::uintptr_t address;void* ship;void* world;std::uint32_t id;};
 Call c{invoke_?invoke_:Dock,api_->va(profile_->dock),ship,static_cast<unsigned char*>(server_)+0x344,result.vehicle_id};
 const bool ok=cc2x_seh_guard([](void* p){auto& c=*static_cast<Call*>(p);c.fn(c.address,c.ship,c.world,c.id);},&c)!=0;
 if(!ok){faulted_=true;Stop(CC2X_RECOVERY_INVALIDATED);return CC2X_DOCK_ENGINE_FAULT;}
 if(!Context()||generation_!=result.generation)return CC2X_DOCK_INVALID_VEHICLE;
 aircraft=api_->find(CC2X_WORLD_VEHICLES,result.vehicle_id);
 return aircraft&&api_->geti(aircraft,CC2X_VEHICLE_DOCK_STATE,0)==5&&api_->geti(aircraft,CC2X_VEHICLE_DOCK_VEHICLE,0)==result.carrier_id?
 CC2X_DOCK_ACCEPTED:CC2X_DOCK_ENGINE_REFUSED;
}
bool Recovery::Arm(const std::string& owner,std::uint64_t generation,std::uint32_t carrier,std::uint32_t vehicle,const cc2x_recovery_options& o){
 std::lock_guard<std::recursive_mutex> l(mutex_);void* s=nullptr;void* a=nullptr;
 if(!Owner(owner)||!Context()||generation!=generation_||status_.armed||used_.count(vehicle)||used_.size()>=64||o.size!=sizeof(o)||!o.window_ticks||o.window_ticks>600||
 !o.grants||o.grants>3||!o.touch_jumps||o.touch_jumps>30||!Available(CC2X_CARRIER_RECOVERY)||!Pair(carrier,vehicle,&s,&a))return false;
 if(api_->geti(a,CC2X_VEHICLE_DOCK_STATE,0)!=5||api_->geti(a,CC2X_VEHICLE_DOCK_VEHICLE,0)!=carrier||!Parked(s,a))return false;
 const bool rotor=api_->geti(a,CC2X_VEHICLE_IS_ROTOR,0)!=0;
 if((!rotor&&!api_->geti(a,CC2X_VEHICLE_IS_WING,0))||(rotor&&o.window_ticks>300)||!Install())return false;
 owner_=owner;status_={sizeof(status_),vehicle,carrier,1,0,0,o.window_ticks,CC2X_RECOVERY_NONE,generation_};
 used_[vehicle]=carrier;deadline_=tick_+o.window_ticks;grantsLeft_=o.grants;jumpsLeft_=o.touch_jumps;rotorLeft_=1;ours_=false;manager_=nullptr;rotor_=rotor;return true;
}
bool Recovery::Disarm(const std::string& owner,std::uint32_t vehicle){
 std::lock_guard<std::recursive_mutex> l(mutex_);if(!Context()||owner!=owner_||vehicle!=status_.vehicle_id)return false;
 Stop(CC2X_RECOVERY_DISARMED);return true;
}
bool Recovery::Status(const std::string& owner,std::uint32_t vehicle,cc2x_recovery_status* out){
 std::lock_guard<std::recursive_mutex> l(mutex_);if(!Context()||owner!=owner_||vehicle!=status_.vehicle_id||!out||out->size!=sizeof(*out))return false;
 if(status_.armed)LiveWindow();*out=status_;out->remaining_ticks=status_.armed?static_cast<std::uint32_t>(deadline_-tick_):0;return true;
}
void Recovery::DropOwner(const std::string& owner){
 std::lock_guard<std::recursive_mutex> l(mutex_);if(owner==owner_){Stop(CC2X_RECOVERY_DISARMED);owner_.clear();}
 requests_.erase(std::remove_if(requests_.begin(),requests_.end(),[&](const auto& r){return r.owner==owner;}),requests_.end());
}
void Recovery::Hook(unsigned site,cc2x_cpu* cpu){
 std::lock_guard<std::recursive_mutex> l(mutex_);if(!cpu||!status_.armed||!Context())return;
 auto before=*cpu;struct Call{Recovery* r;unsigned site;cc2x_cpu* cpu;};Call c{this,site,cpu};bool ok=false;
 try{ok=cc2x_seh_guard([](void* p){auto& c=*static_cast<Call*>(p);c.r->ProcessHook(c.site,c.cpu);},&c)!=0;}catch(...){ok=false;}
 if(!ok){*cpu=before;faulted_=true;Stop(CC2X_RECOVERY_INVALIDATED);}
}
void Recovery::ProcessHook(unsigned site,cc2x_cpu* cpu){
 if(site==0)ours_=false;if(!LiveWindow())return;
 auto* aircraft=api_->find(CC2X_WORLD_VEHICLES,status_.vehicle_id);
 auto* ship=api_->find(CC2X_WORLD_VEHICLES,status_.carrier_id);
 auto* manager=reinterpret_cast<const unsigned char*>(cpu->edi);
 if(site==0&&!rotor_){
 auto* element=reinterpret_cast<const std::uint32_t*>(cpu->eax);
 if(!grantsLeft_||!Region(element,12)||element[0]!=status_.vehicle_id||!Region(manager,0x60))return;
 if(status_.grants&&element[1]==3){Stop(CC2X_RECOVERY_ABORTED);return;}
 fake_[0]=element[0];fake_[1]=1;fake_[2]=element[2];cpu->eax=reinterpret_cast<std::uint32_t>(fake_);ours_=true;manager_=const_cast<unsigned char*>(manager);
 }else if(site==1&&!rotor_){
 const bool ours=ours_;ours_=false;
 if(!ours||!grantsLeft_||manager!=manager_||!Region(manager,0x60)||At<int>(manager,0)!=1||At<int>(manager,0x5c)!=-1)return;
 cpu->jump_to=static_cast<std::uint32_t>(api_->va(profile_->h2next));--grantsLeft_;++status_.grants;
 }else if(site==2&&!rotor_){
 auto* frame=reinterpret_cast<const unsigned char*>(cpu->ebp);
 if(!jumpsLeft_||!Region(frame-0x300,0x310)||At<void*>(frame-0x1fc,0)!=aircraft||cpu->edi!=reinterpret_cast<std::uint32_t>(aircraft)||
 At<void*>(frame-0x200,0)!=ship)return;
 manager=At<const unsigned char*>(frame-0x204,0);
 if(!Region(manager,0xe0)||At<int>(manager,0x1c)!=3||At<std::uint32_t>(manager,0x5c)!=status_.vehicle_id)return;
 const unsigned po[]{0x2e4,0x234,0x23c},qo[]{0x2dc,0x2d4,0x2a4};double speed=0;
 for(int i=0;i<3;++i){const double d=At<float>(frame-po[i],0)-At<float>(frame-qo[i],0);speed+=d*d;}if(!(speed<=4))return;
 cpu->edi=reinterpret_cast<std::uint32_t>(manager);cpu->esi=reinterpret_cast<std::uint32_t>(ship);
 cpu->jump_to=static_cast<std::uint32_t>(api_->va(profile_->h5next));--jumpsLeft_;++status_.touch_jumps;
 }else if(site==3&&rotor_){
 auto* frame=reinterpret_cast<const unsigned char*>(cpu->ebp);
 if(!rotorLeft_||!Region(frame-0x300,0x310)||At<void*>(frame-0x270,0)!=aircraft||At<void*>(frame-0x200,0)!=ship||
 At<void*>(frame-0x204,0)!=manager||!Region(manager,0xe0)||At<int>(manager,0)!=1||At<int>(manager,0x5c)!=-1)return;
 cpu->jump_to=static_cast<std::uint32_t>(api_->va(profile_->h6next));--rotorLeft_;++status_.grants;
 }
}
}
