// SPDX-License-Identifier: GPL-3.0-or-later
// General engine control-state and message-20 transport derived from research
// by Yolo / totolerigolo118, a40d1572eabd6d9710e6d64aae431824ffc98790.
// No named buttons, copying rules, panels or gameplay settings live here.
#include "buttons.h"
#include "platform/seh_guard.h"
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <set>
namespace cc2::api::vehicle {
namespace {
bool Region(const void* p,std::size_t n,bool write=false) {
  MEMORY_BASIC_INFORMATION m{};if(!p||!n||!VirtualQuery(p,&m,sizeof(m))||m.State!=MEM_COMMIT||(m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
  const auto begin=reinterpret_cast<std::uintptr_t>(p),base=reinterpret_cast<std::uintptr_t>(m.BaseAddress);
  if(begin<base||begin-base>m.RegionSize||n>m.RegionSize-(begin-base))return false;
  const auto access=m.Protect&0xff;
  return write?(access==PAGE_READWRITE||access==PAGE_WRITECOPY||access==PAGE_EXECUTE_READWRITE||access==PAGE_EXECUTE_WRITECOPY):
    (access==PAGE_READONLY||access==PAGE_READWRITE||access==PAGE_WRITECOPY||access==PAGE_EXECUTE_READ||access==PAGE_EXECUTE_READWRITE||access==PAGE_EXECUTE_WRITECOPY);
}
template<class T>T At(const unsigned char* p,std::size_t offset){T value{};std::memcpy(&value,p+offset,sizeof(value));return value;}
bool Queues(void* peers,std::vector<void*>* result) {
  auto* p=static_cast<unsigned char*>(peers);if(!Region(p,16))return false;
  auto** base=At<void**>(p,0);auto cap=At<std::uint32_t>(p,4),head=At<std::uint32_t>(p,8),count=At<std::uint32_t>(p,12);
  if(count>cap||cap>64||(cap&&(head>=cap||!Region(base,4*cap))))return false;
  std::set<void*> unique;
  for(std::uint32_t i=0;i<count;++i){auto* player=static_cast<unsigned char*>(base[(head+i)%cap]);if(!Region(player,0x68))return false;
    auto* queue=At<unsigned char*>(player,0x64);if(!Region(queue,0x18,true)||!unique.insert(queue).second)return false;
    auto* data=At<void*>(queue,8);auto capacity=At<std::uint32_t>(queue,12),first=At<std::uint32_t>(queue,16),size=At<std::uint32_t>(queue,20);
    if(size>capacity||capacity>(1u<<20)||(capacity&&(first>=capacity||!Region(data,4*capacity))))return false;
    result->push_back(queue);
  }return true;
}
const cc2x_api* installedApi=nullptr;ButtonsCallback installedCallback=nullptr;void* installedUser=nullptr;
ButtonQueryCallback installedQuery=nullptr;ButtonsInterest installedInterest=nullptr;
const cc2x_api* configuredApi=nullptr;ButtonsCallback configuredCallback=nullptr;void* configuredUser=nullptr;
ButtonQueryCallback configuredQuery=nullptr;ButtonsInterest configuredInterest=nullptr;
ButtonReference queryReference{};
ButtonEnqueue installedEnqueue=nullptr;bool ready=false,faulted=false;
struct CommitCall {ButtonContext* context;};
void Apply(void* opaque) {
  auto& c=*static_cast<CommitCall*>(opaque)->context;
  for(const auto& change:c.pending){auto& button=c.buttons[change.first];if(button.on==change.second)continue;
    button.state[0]=change.second?1:0;
    for(void* queue:c.queues)c.enqueue(queue,c.vehicleId,button.index,change.second?1:0,0);
  }
}
struct Build {const char* name;std::uint32_t timestamp,group,enqueue,absolute[4],query[2],next[2],call[2],reader;};
constexpr Build builds[]{
  {"1.5.18-win32",0x6a36c177,0x70600e,0x565ce0,{0xc97dbd,0xe1db44,0xd1ee70,0xd2523c},
    {0x6fcd62,0x6fd93c},{0x6fcd68,0x6fd942},{0x6fcd74,0x6fd942},0x63f570},
  {"1.5.18-win32-r2",0x6ae228f3,0x70611e,0x565e00,{0xc97f5d,0xe1db44,0xd1eea0,0xd25258},
    {0x6fce72,0x6fda4c},{0x6fce78,0x6fda52},{0x6fce84,0x6fda52},0x63f670}};
const Build* installedBuild=nullptr;
constexpr unsigned char syncBytes[]{0x33,0xc9,0x89,0x4c,0x24,0x14};
constexpr unsigned char queryBytes[]{0x8d,0x8e,0x60,0x05,0,0};
constexpr unsigned char afterFirst[]{0x6a,0x01,0xc7,0x82,0x74,0x01,0,0,0x07,0,0,0};
constexpr unsigned char beforeSecond[]{0x6a,0x01};
constexpr unsigned char readerBytes[]{0x55,0x8b,0xec,0x8b,0x41,0x04,0x85,0xc0,0x74,0x17,0x80,0x38,0,0x74,0x12,
  0x80,0x78,0x02,0,0x75,0x06,0x80,0x7d,0x08,0,0x75,0x06,0xb0,0x01,0x5d,0xc2,0x04,0,0x32,0xc0,0x5d,0xc2,0x04,0};
constexpr unsigned char type20[]{0xb8,0x14,0,0,0,0xc3};
constexpr unsigned char absoluteAt[]{6,0x15,0x38,0x45};
constexpr char groupPattern[]=
 "8b74243433c0894424148b0a85c9740c8039007407c644242301eb09c64424230085c974233987e40500000f94c08801"
 "8b02c74004000000008b0285c074098038007404b001eb0232c038442423746933c9894c2418394e0c765e0f1f800000"
 "00008b0285c0740a803800c6442430017505c6442430008b56088b0603d18b4c241c6a00ff742434ff71fc8bca2b4e04"
 "3b5604ffb7800000000f42ca8b0c888b4964e8????????8b4c24188b54241c41894c24183b4e0c72a98b44241483c20c"
 "408954241c8944241483f8060f8238ffffff33c9894c2414";
constexpr char enqueuePattern[]=
 "558bec6aff68????????64a10000000050515657a1????????33c5508d45f464a3000000008bf16a10e8????????8bf8"
 "83c404897df0c707????????c745fc00000000c707????????c745fcffffffff8b4e148b560c3bca751c8d0412b92000"
 "00003bc10f42c18d4e0850e8????????8b4e148b560c8b46104148894e1403c18bc82bca3bc20f42c88b4608893c888b"
 "45088947048b450c8947088a451088470c8a451488470d8b4df464890d00000000595f5e8be55dc21000";
int Hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
bool Pattern(const cc2x_api* a,std::uint32_t va,const char* pattern) {
  const auto* bytes=reinterpret_cast<const unsigned char*>(a->va(va));auto n=std::strlen(pattern)/2;
  if(!Region(bytes,n))return false;
  for(std::size_t i=0;i<n;++i)if(pattern[2*i]!='?'&&bytes[i]!=Hex(pattern[2*i])*16+Hex(pattern[2*i+1]))return false;
  return true;
}
}
static bool Inventory(void* vehicle,ButtonContext* context) {
  if(!context)return false;*context={};auto* v=static_cast<unsigned char*>(vehicle);
  if(!Region(v,0xdc))return false;
  auto* definition=At<unsigned char*>(v,0x88);auto* states=At<unsigned char*>(v,0xd4);auto n=At<std::uint32_t>(v,0xd8);
  if(!n||n>1024||!Region(states,8*n)||!Region(definition,0x130))return false;
  auto* base=At<unsigned char*>(definition,0x120);auto cap=At<std::uint32_t>(definition,0x124),head=At<std::uint32_t>(definition,0x128),count=At<std::uint32_t>(definition,0x12c);
  if(count!=n||cap<n||cap>4096||head>=cap||!Region(base,cap*0x58))return false;
  ButtonContext result;result.vehicleId=At<std::uint32_t>(v,0x80);
  if(!result.vehicleId)return false;
  for(std::uint32_t i=0;i<n;++i){auto* entry=base+((head+i)%cap)*0x58;auto* name=At<const char*>(entry,0);auto length=At<std::uint32_t>(entry,4);
    // Empty unnamed controls may exist. They are visible in snapshots but
    // cannot be addressed by a mod's named write request.
    if(length>127||(length&&(!Region(name,length+1)||name[length]!=0||std::memchr(name,0,length))))return false;
    result.buttons.push_back({length?std::string(name,length):std::string{},i,states[8*i]!=0,states[8*i+2]!=0,states+8*i,entry});
  }
  *context=std::move(result);return true;
}
bool BeginButtons(void* vehicle,void* peers,ButtonEnqueue enqueue,ButtonContext* context) {
  if(!Inventory(vehicle,context)||!enqueue||!Queues(peers,&context->queues))return false;
  context->enqueue=enqueue;return true;
}
bool BeginButtonQuery(void* vehicle,ButtonContext* context) {
  if(!Inventory(vehicle,context))return false;
  auto* v=static_cast<unsigned char*>(vehicle);if(!Region(v+0x560,sizeof(ButtonReference)))return false;
  auto reference=At<ButtonReference>(v,0x560);
  if(reference.index<0||static_cast<std::size_t>(reference.index)>=context->buttons.size())return false;
  const auto& b=context->buttons[reference.index];
  if(reference.state!=b.state||reference.definition!=b.definition)return false;
  context->sourceIndex=reference.index;return true;
}
bool SelectButtonReference(const ButtonContext& c,int index,ButtonReference* result) {
  if(c.rejected||!result||c.sourceIndex<0||static_cast<std::size_t>(c.sourceIndex)>=c.buttons.size()||index<0||static_cast<std::size_t>(index)>=c.buttons.size())return false;
  const auto& source=c.buttons[c.sourceIndex];const auto& alternate=c.buttons[index];
  if(!Region(source.state,3)||!source.state[2]||!Region(alternate.state,3)||!alternate.state[2])return false;
  *result={static_cast<std::int32_t>(alternate.index),alternate.state,alternate.definition};return true;
}
bool StageButtons(ButtonContext* c,const std::map<std::string,bool>& values) {
  if(!c||!c->enqueue||c->rejected||values.empty()||values.size()>64)return false;
  std::map<std::uint32_t,bool> pending=c->pending;
  for(const auto& value:values){const ButtonState* found=nullptr;
    if(value.first.empty()||value.first.size()>127)return false;
    for(const auto& button:c->buttons)if(button.name==value.first){if(found)return false;found=&button;}
    if(!found||!found->enabled||!Region(found->state,1,true))return false;
    auto existing=pending.find(found->index);
    if(existing!=pending.end()&&existing->second!=value.second){c->rejected=true;return false;}
    pending[found->index]=value.second;
  }
  c->pending=std::move(pending);return true;
}
bool CommitButtons(ButtonContext* c) {
  if(!c||c->rejected||!c->enqueue)return false;
  if(c->pending.empty())return true;
  for(const auto& change:c->pending){auto& b=c->buttons[change.first];if(!Region(b.state,3,true)||!b.state[2]||(b.state[0]!=0)!=b.on)return false;}
  for(void* queue:c->queues)if(!Region(queue,0x18,true))return false;
  CommitCall call{c};return cc2x_seh_guard(Apply,&call)!=0;
}
extern "C" __attribute__((used,noinline)) void cc2x_buttons_hook_body(cc2x_cpu* cpu,void* phase) {
  if(!ready||faulted||!installedCallback)return;
  const auto query=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(phase));
  const char* event=query?"vehicle_control_query":"vehicle_controls";
  if(installedInterest&&!installedInterest(event,installedUser))return;
  if(query) {
    if(query>2||!installedQuery)return;
    ButtonContext context;if(!BeginButtonQuery(reinterpret_cast<void*>(cpu->esi),&context))return;
    const int selected=installedQuery(&context,installedUser);
    if(!SelectButtonReference(context,selected,&queryReference))return;
    cpu->ecx=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&queryReference));
    cpu->jump_to=static_cast<std::uint32_t>(installedApi->va(installedBuild->next[query-1]));return;
  }
  ButtonContext context;
  if(!BeginButtons(reinterpret_cast<void*>(cpu->edi),reinterpret_cast<void*>(cpu->esi),installedEnqueue,&context))return;
  installedCallback(&context,installedUser);
  if(!CommitButtons(&context)&&!context.pending.empty()) {
    // A collision refuses the whole staged batch. A native transport failure
    // has an unknown outcome and disables further writes until process exit.
    if(!context.rejected)faulted=true;
    if(installedApi->log)installedApi->log("cc2x_controls",CC2X_LOG_WARN,"event=BUTTON_BATCH_REJECTED collision=%d transport_disabled=%d",context.rejected,faulted);
  }
}
extern "C" __attribute__((naked,used)) void cc2x_buttons_hook(cc2x_cpu*,void*) {
  __asm__("subl $0x80, %esp\n\t"
    "movups %xmm0, 0x00(%esp)\n\t" "movups %xmm1, 0x10(%esp)\n\t" "movups %xmm2, 0x20(%esp)\n\t" "movups %xmm3, 0x30(%esp)\n\t"
    "movups %xmm4, 0x40(%esp)\n\t" "movups %xmm5, 0x50(%esp)\n\t" "movups %xmm6, 0x60(%esp)\n\t" "movups %xmm7, 0x70(%esp)\n\t"
    "pushl 0x88(%esp)\n\t" "pushl 0x88(%esp)\n\t" "call _cc2x_buttons_hook_body\n\t" "addl $8, %esp\n\t"
    "movups 0x00(%esp), %xmm0\n\t" "movups 0x10(%esp), %xmm1\n\t" "movups 0x20(%esp), %xmm2\n\t" "movups 0x30(%esp), %xmm3\n\t"
    "movups 0x40(%esp), %xmm4\n\t" "movups 0x50(%esp), %xmm5\n\t" "movups 0x60(%esp), %xmm6\n\t" "movups 0x70(%esp), %xmm7\n\t"
    "addl $0x80, %esp\n\t" "ret\n\t");
}
static bool CheckButtons(const cc2x_api* a,ButtonsCallback callback,void* user,ButtonQueryCallback query,ButtonsInterest interest,bool install) {
  if(ready)return installedApi==a&&installedUser==user;
  if(!a||!a->va||!a->game_build||!a->patch_begin||!a->patch_mid||!a->patch_abort||!a->patch_commit||!callback)return false;
  const Build* profile=nullptr;for(const auto& b:builds)if(std::strcmp(b.name,a->game_build)==0)profile=&b;if(!profile)return false;
  const auto* image=reinterpret_cast<const unsigned char*>(a->module_base);
  if(!Region(image,0x40))return false;auto pe=At<std::uint32_t>(image,0x3c);
  if(pe>0x1000||!Region(image+pe,12)||std::memcmp(image+pe,"PE\0\0",4)||At<std::uint32_t>(image+pe,8)!=profile->timestamp)return false;
  if(!Pattern(a,profile->group,groupPattern)||!Pattern(a,profile->enqueue,enqueuePattern))return false;
  const auto* group=reinterpret_cast<const unsigned char*>(a->va(profile->group));
  if(group[0xa2]!=0xe8||reinterpret_cast<std::uintptr_t>(group+0xa7)+At<std::int32_t>(group,0xa3)!=a->va(profile->enqueue))return false;
  const auto* enqueue=reinterpret_cast<const unsigned char*>(a->va(profile->enqueue));
  for(int i=0;i<4;++i)if(At<std::uint32_t>(enqueue,absoluteAt[i])!=a->va(profile->absolute[i]))return false;
  const auto* vtable=reinterpret_cast<const unsigned char*>(a->va(profile->absolute[3]));if(!Region(vtable,8))return false;
  auto* type=reinterpret_cast<const void*>(At<std::uintptr_t>(vtable,4));if(!Region(type,sizeof(type20))||std::memcmp(type,type20,sizeof(type20)))return false;
  const auto* reader=reinterpret_cast<const void*>(a->va(profile->reader));
  if(!Region(reader,sizeof(readerBytes))||std::memcmp(reader,readerBytes,sizeof(readerBytes)))return false;
  for(int i=0;i<2;++i){const auto* call=reinterpret_cast<const unsigned char*>(a->va(profile->call[i]));
    const auto* site=reinterpret_cast<const void*>(a->va(profile->query[i]));
    if(!Region(site,sizeof(queryBytes))||std::memcmp(site,queryBytes,sizeof(queryBytes))||!Region(call,5)||call[0]!=0xe8||
        reinterpret_cast<std::uintptr_t>(call+5)+At<std::int32_t>(call,1)!=a->va(profile->reader))return false;
  }
  const auto* after=reinterpret_cast<const void*>(a->va(profile->next[0]));const auto* before=reinterpret_cast<const void*>(a->va(profile->query[1]-2));
  if(!Region(after,sizeof(afterFirst))||std::memcmp(after,afterFirst,sizeof(afterFirst))||!Region(before,sizeof(beforeSecond))||std::memcmp(before,beforeSecond,sizeof(beforeSecond)))return false;
  if(!install)return true; // capability discovery must leave native consumers' hook sites intact
  a->patch_begin("cc2x.vehicle_buttons");
  if(!a->patch_mid(profile->group+0xd2,sizeof(syncBytes),syncBytes,cc2x_buttons_hook,nullptr)){a->patch_abort();return false;}
  for(int i=0;i<2;++i)if(!a->patch_mid(profile->query[i],sizeof(queryBytes),queryBytes,cc2x_buttons_hook,reinterpret_cast<void*>(static_cast<std::uintptr_t>(i+1)))){a->patch_abort();return false;}
  if(!a->patch_commit())return false;
  installedApi=a;installedCallback=callback;installedUser=user;installedEnqueue=reinterpret_cast<ButtonEnqueue>(a->va(profile->enqueue));
  installedQuery=query;installedInterest=interest;installedBuild=profile;ready=true;return true;
}
bool InstallButtons(const cc2x_api* a,ButtonsCallback callback,void* user,ButtonQueryCallback query,ButtonsInterest interest) {
  return CheckButtons(a,callback,user,query,interest,true);
}
bool ConfigureButtons(const cc2x_api* a,ButtonsCallback callback,void* user,ButtonQueryCallback query,ButtonsInterest interest) {
  if(!CheckButtons(a,callback,user,query,interest,false))return false;
  configuredApi=a;configuredCallback=callback;configuredUser=user;configuredQuery=query;configuredInterest=interest;return true;
}
bool ButtonsSupported(){return configuredApi&&!faulted;}
bool EnsureButtons(){return !faulted&&configuredApi&&InstallButtons(configuredApi,configuredCallback,configuredUser,configuredQuery,configuredInterest);}
bool ButtonsReady(){return ready&&!faulted;}
}
