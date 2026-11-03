// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic profile and message queues. Checks dispatch/ownership/transactions,
// not the correctness of the real game's instruction bytes or network delivery.
// Include the implementation to construct its private profile as test input.
#include "api/vehicle/buttons.cpp"
#include "runtime/host.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdarg>
namespace fs=std::filesystem;
using namespace cc2::api::vehicle;
extern "C" const cc2x_framework_api* cc2x_query_framework_v1(std::uint32_t,std::uint32_t);
extern "C" void cc2x_native_hangar_spawn_result(std::uint32_t,int,std::uint32_t,std::uint32_t){}
namespace {
std::vector<unsigned char> image(0xc00000);
std::wstring modRoot;std::vector<std::string> logs;
int begins=0,aborts=0,commits=0,failAt=0,patches=0,messages=0,nativeCalls=0,queryChoice=1;
struct Patch{std::uint32_t site,len;};std::vector<Patch> pending;
const cc2x_framework_api* framework=nullptr;
std::uintptr_t Va(std::uint32_t va){return va>=0x400000&&va-0x400000<image.size()?reinterpret_cast<std::uintptr_t>(image.data()+va-0x400000):0;}
template<class T>void Put(void* base,std::size_t offset,T value){std::memcpy(static_cast<unsigned char*>(base)+offset,&value,sizeof(value));}
void PatternAt(std::uint32_t address,const char* text){auto* out=reinterpret_cast<unsigned char*>(Va(address));for(std::size_t i=0;i<std::strlen(text)/2;++i)out[i]=text[2*i]=='?'?0:Hex(text[2*i])*16+Hex(text[2*i+1]);}
void BytesAt(std::uint32_t address,const void* bytes,std::size_t size){std::memcpy(reinterpret_cast<void*>(Va(address)),bytes,size);}
void Begin(const char*){++begins;patches=0;pending.clear();}
void Abort(){++aborts;pending.clear();}
int Mid(std::uint32_t site,std::uint32_t len,const std::uint8_t* expected,void(*)(cc2x_cpu*,void*),void*){
  ++patches;if(failAt==patches||std::memcmp(reinterpret_cast<void*>(Va(site)),expected,len))return 0;
  pending.push_back({site,len});return 1;
}
int Commit(){++commits;for(const auto& p:pending)std::memset(reinterpret_cast<void*>(Va(p.site)),0x90,p.len);pending.clear();return 1;}
void Log(const char*,int,const char* format,...){char text[2048]{};va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);logs.emplace_back(text);}
bool Has(const char* text){for(const auto& log:logs)if(log.find(text)!=std::string::npos)return true;return false;}
void TickRegister(void(*)(void*,void*),void*){}
void SessionRegister(void(*)(void*,int,void*),void*){}
int ModCount(){return 1;}
int ModPath(int i,wchar_t* out,std::uint32_t capacity){if(i||modRoot.size()+1>capacity)return 0;std::wcscpy(out,modRoot.c_str());return 1;}
void __attribute__((thiscall)) Enqueue(void*,std::uint32_t id,std::uint32_t index,std::uint32_t value,std::uint32_t flag){if(id==7&&index<2&&value==1&&!flag)++messages;}
std::int32_t NativeControls(const cc2x_framework_event* event,void*){
  if(event->a!=7||framework->button_count(7)!=2)return CC2X_FRAMEWORK_DEFAULT;
  cc2x_button_info button{};if(!framework->button_get(7,1,&button)||std::strcmp(button.name,"example_copy"))return CC2X_FRAMEWORK_DEFAULT;
  if(std::strcmp(event->name,"vehicle_control_query")==0)return queryChoice;
  const cc2x_button_value value{"example_copy",1};if(framework->buttons_set(7,&value,1))++nativeCalls;
  return CC2X_FRAMEWORK_DEFAULT;
}
}
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": " #x "\n";for(const auto& log:logs)std::cerr<<log<<"\n";return 1;}}while(0)
int main(){
  const auto& build=builds[1]; // current reviewed r2; private bytes are synthetic input
  Put(image.data(),0x3c,0x80u);std::memcpy(image.data()+0x80,"PE\0\0",4);Put(image.data(),0x88,build.timestamp);
  PatternAt(build.group,groupPattern);PatternAt(build.enqueue,enqueuePattern);
  auto* group=reinterpret_cast<unsigned char*>(Va(build.group));
  Put(group,0xa3,static_cast<std::int32_t>(Va(build.enqueue)-reinterpret_cast<std::uintptr_t>(group+0xa7)));
  for(int i=0;i<4;++i)Put(reinterpret_cast<void*>(Va(build.enqueue)),absoluteAt[i],static_cast<std::uint32_t>(Va(build.absolute[i])));
  BytesAt(0x403000,type20,sizeof(type20));Put(reinterpret_cast<void*>(Va(build.absolute[3])),4,static_cast<std::uint32_t>(Va(0x403000)));
  BytesAt(build.reader,readerBytes,sizeof(readerBytes));
  for(int i=0;i<2;++i){BytesAt(build.query[i],queryBytes,sizeof(queryBytes));auto* call=reinterpret_cast<unsigned char*>(Va(build.call[i]));
    call[0]=0xe8;Put(call,1,static_cast<std::int32_t>(Va(build.reader)-reinterpret_cast<std::uintptr_t>(call+5)));}
  BytesAt(build.next[0],afterFirst,sizeof(afterFirst));BytesAt(build.query[1]-2,beforeSecond,sizeof(beforeSecond));
  wchar_t temp[MAX_PATH]{};CHECK(GetTempPathW(MAX_PATH,temp)>0);
  auto root=fs::path(temp)/(L"cc2x_buttons_parity_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64()));
  fs::create_directories(root/L"mod/content/bridge");modRoot=(root/L"mod").wstring();
  std::ofstream(root/L"mod/mod.xml")<<"<mod/>";
  std::ofstream(root/L"mod/content/bridge.json")<<R"({"id":"buttons.test","scripts":["bridge/main.lua"]})";
  std::ofstream(root/L"mod/content/bridge/main.lua")<<R"(
local cc2=require('cc2')
return {on_load=function()
  assert(cc2.events.on('vehicle_controls',function(id)
    local buttons=assert(cc2.world.buttons(id));assert(#buttons==2 and buttons[2].name=='example_copy')
    assert(cc2.world.set_buttons(id,{example_button=true}));print('LUA_BUTTON_WRITE_PASS')
  end))
  assert(cc2.events.on('vehicle_control_query',function(id,index)
    assert(id==7 and index==0);assert(cc2.world.buttons(id)[2].on)
    assert(not cc2.world.set_buttons(id,{example_button=true}))
    return 1
  end))
end}
)";
  cc2x_api api{};api.size=sizeof(api);api.api_version=CC2X_API_VERSION;api.game_build=build.name;api.module_base=reinterpret_cast<std::uintptr_t>(image.data());
  api.va=Va;api.patch_begin=Begin;api.patch_abort=Abort;api.patch_commit=Commit;api.patch_mid=Mid;
  api.log=Log;api.on_session=SessionRegister;api.on_server_tick=TickRegister;api.active_mod_count=ModCount;api.active_mod_path=ModPath;
  std::string config=root.string();api.config_dir=config.c_str();
  cc2::runtime::Host host;
  CHECK(ConfigureButtons(&api,[](ButtonContext* c,void* h){static_cast<cc2::runtime::Host*>(h)->ButtonsEvent(c);},&host,
      [](ButtonContext* c,void* h){return static_cast<cc2::runtime::Host*>(h)->ButtonQueryEvent(c);}));
  CHECK(begins==0&&commits==0&&!ButtonsReady()); // discovery consumes no C hook sites
  CHECK(host.Attach(&api,false));framework=cc2x_query_framework_v1(CC2X_FRAMEWORK_VERSION,sizeof(cc2x_framework_api));CHECK(framework);
  auto* occupied=reinterpret_cast<unsigned char*>(Va(build.query[0]));const auto saved=*occupied;*occupied=0xe9;
  CHECK(!framework->subscribe("vehicle_controls",NativeControls,nullptr)&&begins==0); // legacy plugin owns site
  *occupied=saved;failAt=2;
  CHECK(!framework->subscribe("vehicle_controls",NativeControls,nullptr)&&aborts==1&&commits==0);
  CHECK(std::memcmp(reinterpret_cast<void*>(Va(build.group+0xd2)),syncBytes,sizeof(syncBytes))==0);
  failAt=0;auto server=reinterpret_cast<void*>(0x12340000);host.Session(server,true);host.Tick(server);
  CHECK(host.Stats().modules==1&&host.Stats().errors==0&&ButtonsReady()&&commits==1&&patches==3);
  CHECK(framework->subscribe("vehicle_controls",NativeControls,nullptr));CHECK(framework->subscribe("vehicle_control_query",NativeControls,nullptr));CHECK(commits==1);
  unsigned char vehicle[0x600]{},definition[0x140]{},entries[2*0x58]{},states[16]{};
  const char first[]="example_button",second[]="example_copy";
  Put(vehicle,0x80,7u);Put(vehicle,0x88,definition);Put(vehicle,0xd4,states);Put(vehicle,0xd8,2u);
  Put(definition,0x120,entries);Put(definition,0x124,2u);Put(definition,0x12c,2u);
  Put(entries,0,first);Put(entries,4,static_cast<std::uint32_t>(sizeof(first)-1));Put(entries,0x58,second);Put(entries,0x5c,static_cast<std::uint32_t>(sizeof(second)-1));states[2]=states[10]=1;
  unsigned char peers[16]{},player[0x70]{},queue[0x18]{};void* players[]{player};
  Put(peers,0,players);Put(peers,4,1u);Put(peers,12,1u);Put(player,0x64,queue);
  ButtonContext controls;CHECK(BeginButtons(vehicle,peers,Enqueue,&controls));host.ButtonsEvent(&controls);
  CHECK(nativeCalls==1&&Has("LUA_BUTTON_WRITE_PASS")&&messages==0&&states[0]==0&&states[8]==0);
  CHECK(CommitButtons(&controls)&&messages==2&&states[0]==1&&states[8]==1);
  CHECK(framework->button_count(7)==-1);cc2x_button_value value{"example_copy",0};CHECK(!framework->buttons_set(7,&value,1));
  states[0]=0;Put(vehicle,0x560,0);Put(vehicle,0x564,states);Put(vehicle,0x568,entries);
  ButtonContext query;CHECK(BeginButtonQuery(vehicle,&query));CHECK(host.ButtonQueryEvent(&query)==1);
  ButtonReference alias;CHECK(SelectButtonReference(query,1,&alias)&&alias.state[0]==1&&states[0]==0);
  queryChoice=0;CHECK(host.ButtonQueryEvent(&query)==-1&&Has("EVENT_DECISION_CONFLICT"));
  host.Shutdown();CHECK(fs::weakly_canonical(root).parent_path()==fs::weakly_canonical(fs::path(temp)));
  std::error_code error;fs::remove_all(root,error);
  std::cout<<"PASS C / real Lua buttons: identical staged transport, immediate read aliases, conflicts, lazy three-hook transaction and preservation of legacy C hook ownership (synthetic engine)\n";
}
