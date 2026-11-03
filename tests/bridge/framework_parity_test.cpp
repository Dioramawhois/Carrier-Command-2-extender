// SPDX-License-Identifier: GPL-3.0-or-later
// Paired public C / real Lua over a synthetic game. No plugin DLL is loaded.
#include "runtime/host.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdarg>
#include <cstring>
#include <vector>
#include <cmath>
#include <thread>
namespace fs=std::filesystem;
using cc2::runtime::Host;
extern "C" const cc2x_framework_api* cc2x_query_framework_v1(std::uint32_t,std::uint32_t);
extern "C" void cc2x_native_bridge_plugin_begin(const wchar_t*);
extern "C" int cc2x_native_bridge_plugin_end(int);
extern "C" void cc2x_native_hangar_spawn_result(std::uint32_t,int,std::uint32_t,std::uint32_t){}
namespace {
std::vector<std::wstring> roots;std::vector<std::string> logs;
const cc2x_framework_api* nativeFramework=nullptr;cc2x_vm* savedVm=nullptr;
int team=1,nativeEvents=0,nativeTasks=0;
double hp=100;
alignas(16) unsigned char aircraft[0x400]{};
unsigned char throttleCode[]{0xf3,0x0f,0x11,0x87,0x14,0x03,0,0};
void Write(const fs::path& p,const std::string& text){fs::create_directories(p.parent_path());std::ofstream(p,std::ios::binary)<<text;}
bool Has(const char* text){for(const auto& log:logs)if(log.find(text)!=std::string::npos)return true;return false;}
void Log(const char*,int,const char* format,...){char text[4096]{};va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);logs.emplace_back(text);}
int ModCount(){return static_cast<int>(roots.size());}
int ModPath(int i,wchar_t* out,std::uint32_t size){if(i<0||static_cast<std::size_t>(i)>=roots.size()||roots[i].size()+1>size)return 0;std::wcscpy(out,roots[i].c_str());return 1;}
void TickRegister(void(*)(void*,void*),void*){}
void SessionRegister(void(*)(void*,int,void*),void*){}
void* Find(int list,std::uint32_t id){return list==CC2X_WORLD_VEHICLES&&id==7?aircraft:nullptr;}
std::int64_t GetI(const void* object,int field,int){if(object!=aircraft)return 0;return field==CC2X_VEHICLE_ID?7:field==CC2X_VEHICLE_IS_WING?1:field==CC2X_VEHICLE_TEAM?team:0;}
double GetF(const void* object,int field,int){return object==aircraft&&field==CC2X_VEHICLE_HP?hp:0;}
int SetI(void* object,int field,int,std::int64_t value){if(object!=aircraft||field!=CC2X_VEHICLE_TEAM)return 0;team=static_cast<int>(value);return 1;}
int SetF(void* object,int field,int,double value){if(object!=aircraft||field!=CC2X_VEHICLE_HP)return 0;hp=value;return 1;}
int Count(const void*,int){return 0;}
void* Ref(const void*,int,int){return nullptr;}
int SubscribeWaypoints(void(*)(void*,void*),void*){return 1;}
bool KeySample(int key){return key==65;}
std::int32_t NativeWaypoint(const cc2x_framework_event* event,void*){if(std::strcmp(event->name,"waypoints")==0&&event->a==7)++nativeEvents;return CC2X_FRAMEWORK_DEFAULT;}
std::int32_t NativeTask(const cc2x_framework_event* event,void*){
  if(std::strcmp(event->name,"task")==0&&event->generation==nativeFramework->session_generation()){
    ++nativeTasks;if(nativeTasks==1)nativeFramework->post("native.parity",event->generation,NativeTask,nullptr);
  }return CC2X_FRAMEWORK_DEFAULT;
}
std::uintptr_t Va(std::uint32_t address){return address==0x71dcbf?reinterpret_cast<std::uintptr_t>(throttleCode):0;}
int NativeSum(cc2x_vm* vm,void*){savedVm=vm;std::int64_t a=0,b=0;if(!nativeFramework||nativeFramework->vm->gettop(vm)!=2||!nativeFramework->vm->tointeger(vm,1,&a)||!nativeFramework->vm->tointeger(vm,2,&b))return 0;nativeFramework->vm->pushinteger(vm,a+b);return 1;}
}
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": " #x "\n";for(const auto& log:logs)std::cerr<<log<<"\n";return 1;}}while(0)
int main(){
  wchar_t temp[MAX_PATH]{};CHECK(GetTempPathW(MAX_PATH,temp)>0);
  const auto root=fs::path(temp)/(L"cc2x_framework_native_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64()));
  cc2x_api api{};api.size=sizeof(api);api.api_version=CC2X_API_VERSION;api.game_build="1.5.18-win32-r2";
  api.log=Log;api.active_mod_count=ModCount;api.active_mod_path=ModPath;api.on_server_tick=TickRegister;api.on_session=SessionRegister;
  api.find=Find;api.geti=GetI;api.getf=GetF;api.seti=SetI;api.setf=SetF;api.count=Count;api.ref=Ref;
  api.on_waypoints=SubscribeWaypoints;api.va=Va;std::string config=root.string();api.config_dir=config.c_str();
  auto server=reinterpret_cast<void*>(0x12340000);
  auto* throttle=reinterpret_cast<float*>(aircraft+0x314);*throttle=0.8f;
  {
    const auto provider=root/L"native_\u0411",consumer=root/L"native_consumer";
    Write(provider/L"mod.xml","<mod/>");fs::create_directories(provider/L"content/cc2x");
    Write(provider/L"content/game_objects/native_plane.xml","<definition/>\n");
    Write(provider/L"content/icons/plane.png","metadata fixture; no image decoding acceptance claimed");
    Write(consumer/L"mod.xml","<mod/>");
    Write(consumer/L"content/bridge.json",R"({"id":"native.consumer","scripts":["bridge/main.lua"]})");
    Write(consumer/L"content/bridge/main.lua",R"(
local cc2=require('cc2')
local v,complete=false,false
local updates,deferred,original_generation=0,0,0
return {on_load=function()
  local record=assert(cc2.vehicles.lookup('native.resources.plane'))
  assert(record.definition_id==102 and record.base_type==8 and not record.ready)
  assert(#cc2.vehicles.list()==1)
  assert(cc2.native.provider.sum(2,3)==5)
  assert(not cc2.vehicles.spawn_in_hangar({type=record.identity,carrier=7,slot=0}))
  v=assert(cc2.world.find(cc2.world.fields.CC2X_WORLD_VEHICLES,7))
  assert(math.abs(v:throttle()-.8)<.00001)
  assert(v:throttle(.7) and not v:throttle(2))
  assert(cc2.input.key_down(65) and not cc2.input.key_down(66))
  assert(cc2.entities.register('fixture:lua',{base='wing_light',definition='native_plane'}))
  assert(not cc2.entities.register('bad\0:hidden',{base='wing_light',definition='native_plane'}))
  assert(cc2.events.on('waypoints',function(id) assert(id==7);print('LUA_WAYPOINT_PASS') end))
  original_generation=cc2.session.generation();assert(original_generation>0)
  assert(cc2.session.post(function()
    deferred=1;assert(cc2.session.post(function() deferred=2 end))
  end))
  local canceled=assert(cc2.session.post(function() error('CANCELED_LUA_TASK_RAN') end))
  assert(cc2.session.cancel(canceled) and not cc2.session.cancel(canceled))
  print('C_DECLARATION_LUA_LOOKUP_PASS')
end,on_update=function()
  updates=updates+1
  assert(deferred==(updates==1 and 0 or updates==2 and 1 or 2))
  if updates==3 then assert(cc2.session.post(function() error('STALE_LUA_TASK_RAN') end)) end
  if updates==4 then assert(cc2.session.generation()~=original_generation);print('LUA_STALE_TASK_DROPPED_PASS') end
  if complete or cc2.messages.read('native.parity','state',7)~=11 then return end
  local request=assert(cc2.messages.poll());assert(#request==1 and request[1].value==2)
  assert(cc2.entities.count()==2)
  local entries=cc2.entities.types();local seen=false
  for _,entry in ipairs(entries) do if entry.id=='fixture:native' then seen=true end end
  assert(seen and not cc2.entities.remove('fixture:native'))
  local result=assert(cc2.carrier.hangar_spawn_result(71))
  assert(result.request_id==71 and result.success and result.vehicle_id==12)
  assert(v:geti(cc2.world.fields.CC2X_VEHICLE_TEAM,0)==3)
  assert(v:getf(cc2.world.fields.CC2X_VEHICLE_HP,0)==75)
  assert(math.abs(v:throttle()-.4)<.00001 and v:throttle(.6))
  assert(v:seti(cc2.world.fields.CC2X_VEHICLE_TEAM,0,4))
  assert(v:setf(cc2.world.fields.CC2X_VEHICLE_HP,0,60))
  assert(cc2.messages.send('native.parity','reply',7,13))
  assert(cc2.messages.publish('state',7,17))
  assert(cc2.entities.remove('fixture:lua') and cc2.entities.clear()==0)
  complete=true;print('C_LUA_WORLD_MESSAGES_ENTITIES_PASS')
end}
)");
    roots={provider.wstring(),consumer.wstring()};Host declarations(KeySample);
    CHECK(declarations.Attach(&api,false,true));
    nativeFramework=cc2x_query_framework_v1(CC2X_FRAMEWORK_VERSION,sizeof(cc2x_framework_api));
    CHECK(nativeFramework&&nativeFramework->declare_vehicle&&nativeFramework->declare_ui);
    CHECK(!cc2x_query_framework_v1(999,sizeof(cc2x_framework_api)));
    CHECK(!cc2x_query_framework_v1(CC2X_FRAMEWORK_VERSION,sizeof(cc2x_framework_api)-1));
    CHECK(!nativeFramework->throttle_set(7,.4));
    CHECK(!nativeFramework->entity_register("native.parity","fixture:native","wing_light","native_plane"));
    const auto mailbox=nativeFramework->mailbox_open("native.parity");CHECK(mailbox);
    const auto event=nativeFramework->subscribe("waypoints",NativeWaypoint,nullptr);CHECK(event);
    cc2x_vehicle_declaration vehicle{sizeof(vehicle),"plane","Native plane","wing_light","game_objects/native_plane.xml","plane_icon",UINT32_MAX};
    cc2x_ui_declaration image{sizeof(image),"plane_icon",nullptr,"icons/plane.png",nullptr,nullptr,0};
    CHECK(!nativeFramework->declare_vehicle("native.resources",&vehicle)); // no current plugin_load scope
    const auto directory=(provider/L"content/cc2x").wstring();
    cc2x_native_bridge_plugin_begin(directory.c_str());
    auto rolledBack=nativeFramework->vm_bind("rollback","sum",NativeSum,nullptr);CHECK(rolledBack);
    CHECK(nativeFramework->declare_ui("native.resources",&image));CHECK(nativeFramework->declare_vehicle("native.resources",&vehicle));
    CHECK(!cc2x_native_bridge_plugin_end(0));CHECK(!nativeFramework->vm_unbind(rolledBack));
    cc2x_native_bridge_plugin_begin(directory.c_str());
    CHECK(nativeFramework->declare_ui("native.resources",&image));CHECK(nativeFramework->declare_vehicle("native.resources",&vehicle));
    CHECK(nativeFramework->vm_bind("provider","sum",NativeSum,nullptr));CHECK(cc2x_native_bridge_plugin_end(1));
    wchar_t oldLocalAppData[32768]{};const auto oldLocalLength=GetEnvironmentVariableW(L"LOCALAPPDATA",oldLocalAppData,32768);
    const auto local=(root/L"fixture-local-cache").wstring();SetEnvironmentVariableW(L"LOCALAPPDATA",local.c_str());
    declarations.PrepareStartupCatalog();
    SetEnvironmentVariableW(L"LOCALAPPDATA",oldLocalLength?oldLocalAppData:nullptr);
    CHECK(nativeFramework->vehicle_definition_count()==1);
    cc2x_vehicle_definition record{};CHECK(nativeFramework->vehicle_definition_get(0,&record));
    CHECK(record.definition_id==102&&record.base_type==8&&std::strcmp(record.identity,"native.resources.plane")==0);
    CHECK(!nativeFramework->vehicle_definition_get(1,&record));CHECK(!nativeFramework->vehicle_spawn("native.resources.plane",7,0));
    cc2x_native_bridge_plugin_begin(directory.c_str());CHECK(!nativeFramework->declare_vehicle("native.resources",&vehicle));CHECK(cc2x_native_bridge_plugin_end(1));
    declarations.Session(server,true);
    const auto generation=nativeFramework->session_generation();CHECK(generation>0);
    CHECK(!nativeFramework->post("native.parity",generation+1,NativeTask,nullptr));
    const auto canceled=nativeFramework->post("native.parity",generation,NativeTask,nullptr);CHECK(canceled);
    CHECK(!nativeFramework->cancel("native.other",canceled));
    CHECK(nativeFramework->cancel("native.parity",canceled)&&!nativeFramework->cancel("native.parity",canceled));
    CHECK(nativeFramework->post("native.parity",generation,NativeTask,nullptr));
    declarations.Tick(server);CHECK(nativeTasks==1);
    CHECK(declarations.Stats().modules==1&&declarations.Stats().errors==0&&Has("C_DECLARATION_LUA_LOOKUP_PASS"));
    CHECK(savedVm&&nativeFramework->vm->gettop(savedVm)==-1);
    double value=0;CHECK(nativeFramework->throttle_get(7,&value)&&std::abs(value-.7)<.00001);
    CHECK(nativeFramework->throttle_set(7,.4)&&!nativeFramework->throttle_set(7,2)&&!nativeFramework->throttle_set(99,.5));
    CHECK(nativeFramework->key_down(65)&&!nativeFramework->key_down(66));
    CHECK(api.seti(api.find(CC2X_WORLD_VEHICLES,7),CC2X_VEHICLE_TEAM,0,3));
    CHECK(api.setf(api.find(CC2X_WORLD_VEHICLES,7),CC2X_VEHICLE_HP,0,75));
    CHECK(nativeFramework->entity_count()==1);
    cc2x_entity_metadata metadata{};CHECK(nativeFramework->entity_get(0,&metadata)&&std::strcmp(metadata.id,"fixture:lua")==0);
    CHECK(!nativeFramework->entity_remove("native.parity","fixture:lua"));
    CHECK(nativeFramework->entity_register("native.parity","fixture:native","wing_light","native_plane"));
    CHECK(!nativeFramework->entity_register("native.parity","fixture:native","wing_light","native_plane"));
    CHECK(nativeFramework->mailbox_publish(mailbox,"state",7,11));
    declarations.RecordHangarSpawnResult(71,1,12,0);
    cc2x_hangar_result result{};CHECK(nativeFramework->hangar_result(71,&result)&&result.success==1&&result.vehicle_id==12);
    std::vector<cc2::runtime::Host::HangarSlotOption> slots;std::string slotReason;
    CHECK(!declarations.DescribeHangarSlots(7,8,&slots,&slotReason)&&slotReason!="wrong_execution_context_or_no_session"); // C server context reaches the real profile guard
    CHECK(nativeFramework->message_send("native.consumer","request",7,2));
    declarations.Tick(server);CHECK(nativeTasks==2&&Has("C_LUA_WORLD_MESSAGES_ENTITIES_PASS"));
    CHECK(nativeFramework->throttle_get(7,&value)&&std::abs(value-.6)<.00001);
    CHECK(api.geti(aircraft,CC2X_VEHICLE_TEAM,0)==4&&api.getf(aircraft,CC2X_VEHICLE_HP,0)==60);
    CHECK(nativeFramework->message_read("native.consumer","state",7,&value)&&value==17);
    cc2x_message messages[64]{};CHECK(nativeFramework->mailbox_poll(mailbox,messages,64)==1);
    CHECK(std::strcmp(messages[0].topic,"reply")==0&&messages[0].value==13);
    declarations.WorldEvent("waypoints",7);CHECK(nativeEvents==1&&Has("LUA_WAYPOINT_PASS"));
    CHECK(nativeFramework->unsubscribe(event)&&!nativeFramework->unsubscribe(event));
    CHECK(nativeFramework->entity_clear("native.parity")==1&&nativeFramework->entity_count()==0);
    std::uint64_t workerTask=0;bool workerRead=true;
    std::thread worker([&]{double ignored=0;workerRead=nativeFramework->throttle_get(7,&ignored);
      workerTask=nativeFramework->post("native.parity",generation,NativeTask,nullptr);});worker.join();
    CHECK(workerTask&&!workerRead); // work may be queued from another thread; engine access may not
    declarations.Tick(server);CHECK(nativeFramework->session_tick()==3&&nativeTasks==3);
    std::vector<std::uint64_t> queued;
    for(int i=0;i<64;++i){const auto id=nativeFramework->post("native.parity",generation,NativeTask,nullptr);CHECK(id);queued.push_back(id);}
    CHECK(!nativeFramework->post("native.parity",generation,NativeTask,nullptr));
    const auto otherTask=nativeFramework->post("native.other",generation,NativeTask,nullptr);CHECK(otherTask);
    CHECK(!nativeFramework->cancel("native.parity",otherTask)&&nativeFramework->cancel("native.other",otherTask));
    for(auto id:queued)CHECK(nativeFramework->cancel("native.parity",id));
    CHECK(nativeFramework->post("native.parity",generation,NativeTask,nullptr)); // must be discarded on session end
    declarations.Session(server,false);
    CHECK(nativeFramework->session_generation()==0&&nativeFramework->session_tick()==0);
    CHECK(!nativeFramework->hangar_result(71,&result));
    CHECK(!nativeFramework->post("native.parity",generation,NativeTask,nullptr));
    CHECK(!nativeFramework->throttle_get(7,&value)&&!nativeFramework->message_read("native.consumer","state",7,&value));
    CHECK(!nativeFramework->message_send("native.consumer","request",7,2));
    declarations.Session(server,true);declarations.Tick(server);
    CHECK(nativeFramework->session_generation()!=generation&&nativeTasks==3&&Has("LUA_STALE_TASK_DROPPED_PASS"));
    declarations.Shutdown();
  }
  CHECK(fs::weakly_canonical(root).parent_path()==fs::weakly_canonical(fs::path(temp)));
  std::error_code error;fs::remove_all(root,error);
  std::cout<<"PASS paired public C / real Lua: world writes, throttle, input, events, bidirectional messages, metadata, C asset declarations, native bindings and bounded session-safe tasks (synthetic engine)\n";
  return 0;
}
