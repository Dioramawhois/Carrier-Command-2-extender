#include "host.h"
#include "sdk_data.h"
#include "mod_loader.h"
#include "entity_registry.h"
#include "../api/animation/animation_speed.h"
#include "../api/carrier/hangar_spawn.h"
#include "internal.h"
#include "../api/carrier/throttle_step.h"
#include "../api/ui/atlas_factory.h"
#include "../api/ui/resource_registry.h"
#include "../api/ui/second_atlas.h"
#include "../api/vehicle/definition_adapter.h"
#include "api/vehicle/definition_registry.h"
#include "api/vehicle/lua_api.h"
#include "api/vehicle/control.h"
#include "platform/seh_guard.h"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <algorithm>
#include <climits>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <cstdlib>

namespace cc2::runtime {
struct Module {
  Host* host=nullptr;
  lua_State* L=nullptr;
  std::string id;
  host::ModDescriptor descriptor;
  std::wstring sdkPath;
  bool rules=false;
  std::set<std::string> subscriptions;
  unsigned long long generation=0,updates=0,callbacks=0,errors=0;
  struct Counter {unsigned long long calls=0,errors=0;};
  std::map<unsigned,Counter> handlers;
  unsigned long long budget=0;
  std::size_t allocated=0;
};
namespace {
const cc2x_api* g_stockLuaApi=nullptr;
Host* g_stockHost=nullptr;

bool StockText(cc2x_lua* L,int at,std::string* result,std::size_t limit) {
  const auto* a=g_stockLuaApi;if(!a||a->lua_type(L,at)!=CC2X_LUA_STRING)return false;
  std::size_t length=0;const char* text=(a->lua_tostring)(L,at,&length);
  if(!text||!length||length>limit||std::memchr(text,'\0',length))return false;
  result->assign(text,length);return true;
}
int StockMessageSend(cc2x_lua* L) {
  const auto* a=g_stockLuaApi;if(!a)return 0;
  std::string owner,topic;int64_t id=0;double value=0;
  const bool valid=g_stockHost&&a->lua_gettop(L)==4&&StockText(L,1,&owner,128)&&StockText(L,2,&topic,64)&&
    a->lua_type(L,3)==CC2X_LUA_NUMBER&&(a->lua_tointeger)(L,3,&id)&&id>=0&&id<=UINT32_MAX&&
    a->lua_type(L,4)==CC2X_LUA_NUMBER&&(a->lua_tonumber)(L,4,&value)&&std::isfinite(value);
  a->lua_pushboolean(L,valid&&g_stockHost->Mailbox().Send(owner,topic,static_cast<std::uint32_t>(id),value));return 1;
}
int StockMessageRead(cc2x_lua* L) {
  const auto* a=g_stockLuaApi;if(!a)return 0;
  std::string owner,topic;int64_t id=0;double value=0;
  if(g_stockHost&&a->lua_gettop(L)==3&&StockText(L,1,&owner,128)&&StockText(L,2,&topic,64)&&
      a->lua_type(L,3)==CC2X_LUA_NUMBER&&(a->lua_tointeger)(L,3,&id)&&id>=0&&id<=UINT32_MAX&&
      g_stockHost->Mailbox().Read(owner,topic,static_cast<std::uint32_t>(id),&value))a->lua_pushnumber(L,value);
  else a->lua_pushnil(L);return 1;
}
int AirfieldIndex(Host* host,void* airfield) {
  const auto* a=host->Api();if(!a->find||!a->geti||!a->ref||!a->count)return -1;
  auto* tile=a->find(CC2X_WORLD_TILES,static_cast<std::uint32_t>(a->geti(airfield,CC2X_AIRFIELD_TILE,0)));
  auto* island=tile?a->ref(tile,CC2X_TILE_ISLAND,0):nullptr;if(!island)return -1;
  int count=a->count(island,CC2X_ISLAND_AIRFIELDS);if(count<0||count>256)return -1;
  for(int i=0;i<count;++i)if(a->ref(island,CC2X_ISLAND_AIRFIELDS,i)==airfield)return i;
  return -1;
}
void WorldWaypoints(void* vehicle,void* user) {
  auto* h=static_cast<Host*>(user);if(vehicle&&h->Api()->geti)h->WorldEvent("waypoints",static_cast<double>(h->Api()->geti(vehicle,CC2X_VEHICLE_ID,0)));
}
void WorldAirfield(void* airfield,void* user) {
  auto* h=static_cast<Host*>(user);if(!airfield||!h->Api()->geti)return;
  const int index=AirfieldIndex(h,airfield);if(index>=0)h->WorldEvent("airfield_tick",static_cast<double>(h->Api()->geti(airfield,CC2X_AIRFIELD_TILE,0)),index);
}
int WorldHangar(void* airfield,void* vehicle,void* user) {
  auto* h=static_cast<Host*>(user);if(!airfield||!vehicle||!h->Api()->geti)return -1;
  const int index=AirfieldIndex(h,airfield);if(index<0)return -1;
  return h->WorldEvent("hangar_choice",static_cast<double>(h->Api()->geti(vehicle,CC2X_VEHICLE_ID,0)),static_cast<double>(h->Api()->geti(airfield,CC2X_AIRFIELD_TILE,0)),index,-1,true);
}
int WorldController(int team,void* user){return static_cast<Host*>(user)->WorldEvent("island_controller",team,0,0,0,true);}
int WorldSpawn(void* tile,int aiTeam,void* user) {
  auto* h=static_cast<Host*>(user);if(!tile||!h->Api()->geti)return 1;
  return h->WorldEvent("island_spawn",static_cast<double>(h->Api()->geti(tile,CC2X_TILE_ID,0)),aiTeam,0,1,true);
}
std::string Utf8(const std::wstring& value) {
  int n=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
  std::string s(n,'\0');if(n)WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),s.data(),n,nullptr,nullptr);return s;
}
std::wstring Wide(const char* value) {
  if(!value)return {};
  int n=MultiByteToWideChar(CP_ACP,0,value,-1,nullptr,0);
  std::wstring s(n,L'\0');if(n)MultiByteToWideChar(CP_ACP,0,value,-1,s.data(),n);if(n)s.pop_back();return s;
}
std::wstring WideUtf8(const char* value,std::size_t length) {
  if(!value||!length||length>static_cast<std::size_t>(INT_MAX)||std::memchr(value,'\0',length))return {};
  const auto size=static_cast<int>(length);
  const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value,size,nullptr,0);
  if(count<=0)return {};
  std::wstring result(static_cast<std::size_t>(count),L'\0');
  if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value,size,result.data(),count)!=count)return {};
  return result;
}
Module* Current(lua_State* L) {return static_cast<Module*>(lua_touserdata(L,lua_upvalueindex(1)));}
int EventSubscribe(lua_State* L) {
  auto* m=Current(L);std::size_t length=0;const char* name=lua_type(L,1)==LUA_TSTRING?lua_tolstring(L,1,&length):nullptr;
  if(lua_gettop(L)!=2||!name||length>64||std::memchr(name,0,length)||!lua_isboolean(L,2)||!m->host->EventAvailable(std::string(name,length),m->rules)) {
    lua_pushboolean(L,0);return 1;
  }
  const std::string event(name,length);
  if(lua_toboolean(L,2)&&(event=="vehicle_controls"||event=="vehicle_control_query")&&!api::vehicle::EnsureButtons()){lua_pushboolean(L,0);return 1;}
  if(lua_toboolean(L,2))m->subscriptions.insert(std::string(name,length));else m->subscriptions.erase(std::string(name,length));
  lua_pushboolean(L,1);return 1;
}
int BoundNative(lua_State* L) {
  auto* m=Current(L);const auto id=static_cast<std::uint64_t>(lua_tointeger(L,lua_upvalueindex(2)));
  return InvokeNativeBinding(L,m->host,id);
}
struct NativeEventCall {cc2x_framework_event_fn callback;const cc2x_framework_event* event;void* user;std::int32_t result=CC2X_FRAMEWORK_DEFAULT;};
void CallNativeEvent(void* context){auto* call=static_cast<NativeEventCall*>(context);call->result=call->callback(call->event,call->user);}
bool FrameworkName(const std::string& value,std::size_t limit=64) {
  if(value.empty()||value.size()>limit)return false;
  for(unsigned char c:value)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-'))return false;
  return true;
}
int MessagePoll(lua_State* L) {
  auto* m=Current(L);
  if(lua_gettop(L)!=0||!m->host->InLuaContext()){lua_pushnil(L);lua_pushliteral(L,"server_callback_required");return 2;}
  auto messages=m->host->Mailbox().Drain(m->id);lua_createtable(L,static_cast<int>(messages.size()),0);
  int i=1;for(const auto& message:messages){lua_createtable(L,0,3);
    lua_pushstring(L,message.topic.c_str());lua_setfield(L,-2,"topic");
    lua_pushinteger(L,message.subject);lua_setfield(L,-2,"subject");
    lua_pushnumber(L,message.value);lua_setfield(L,-2,"value");lua_rawseti(L,-2,i++);}
  return 1;
}
int MessagePublish(lua_State* L) {
  auto* m=Current(L);std::size_t length=0;const char* topic=lua_type(L,1)==LUA_TSTRING?lua_tolstring(L,1,&length):nullptr;
  int idOk=0,valueOk=0;const auto id=lua_tointegerx(L,2,&idOk);const double value=lua_tonumberx(L,3,&valueOk);
  const bool valid=lua_gettop(L)==3&&m->host->InLuaContext()&&topic&&length>0&&length<=64&&!std::memchr(topic,'\0',length)&&
      lua_type(L,2)==LUA_TNUMBER&&idOk&&id>=0&&id<=UINT32_MAX&&lua_type(L,3)==LUA_TNUMBER&&valueOk&&std::isfinite(value);
  lua_pushboolean(L,valid&&m->host->Mailbox().Publish(m->id,std::string(topic,length),static_cast<std::uint32_t>(id),value));return 1;
}
int MessageForget(lua_State* L) {
  auto* m=Current(L);int ok=0;auto subject=lua_tointegerx(L,1,&ok);
  if(lua_gettop(L)!=1||lua_type(L,1)!=LUA_TNUMBER||!ok||subject<0||subject>UINT32_MAX||!m->host->InLuaContext()){lua_pushboolean(L,0);return 1;}
  m->host->Mailbox().Forget(m->id,static_cast<std::uint32_t>(subject));lua_pushboolean(L,1);return 1;
}
int MessageSend(lua_State* L) {
  auto* m=Current(L);std::size_t ownerSize=0,topicSize=0;
  const char* owner=lua_type(L,1)==LUA_TSTRING?lua_tolstring(L,1,&ownerSize):nullptr;
  const char* topic=lua_type(L,2)==LUA_TSTRING?lua_tolstring(L,2,&topicSize):nullptr;
  int subjectOk=0,valueOk=0;auto subject=lua_tointegerx(L,3,&subjectOk);double value=lua_tonumberx(L,4,&valueOk);
  const bool valid=lua_gettop(L)==4&&m->host->InLuaContext()&&owner&&ownerSize>0&&ownerSize<=128&&!std::memchr(owner,0,ownerSize)&&
    topic&&topicSize>0&&topicSize<=64&&!std::memchr(topic,0,topicSize)&&lua_type(L,3)==LUA_TNUMBER&&subjectOk&&subject>=0&&subject<=UINT32_MAX&&
    lua_type(L,4)==LUA_TNUMBER&&valueOk&&std::isfinite(value);
  lua_pushboolean(L,valid&&m->host->Mailbox().Send(std::string(owner,ownerSize),std::string(topic,topicSize),static_cast<std::uint32_t>(subject),value));return 1;
}
int MessageRead(lua_State* L) {
  auto* m=Current(L);std::size_t ownerSize=0,topicSize=0;
  const char* owner=lua_type(L,1)==LUA_TSTRING?lua_tolstring(L,1,&ownerSize):nullptr;
  const char* topic=lua_type(L,2)==LUA_TSTRING?lua_tolstring(L,2,&topicSize):nullptr;
  int ok=0;const auto subject=lua_tointegerx(L,3,&ok);double value=0;
  const bool valid=lua_gettop(L)==3&&m->host->InLuaContext()&&owner&&ownerSize>0&&ownerSize<=128&&!std::memchr(owner,0,ownerSize)&&
    topic&&topicSize>0&&topicSize<=64&&!std::memchr(topic,0,topicSize)&&lua_type(L,3)==LUA_TNUMBER&&ok&&subject>=0&&subject<=UINT32_MAX;
  if(valid&&m->host->Mailbox().Read(std::string(owner,ownerSize),std::string(topic,topicSize),static_cast<std::uint32_t>(subject),&value))lua_pushnumber(L,value);
  else lua_pushnil(L);return 1;
}
void* Allocate(void*ud,void*ptr,size_t oldSize,size_t newSize) {
  auto*m=static_cast<Module*>(ud);if(!ptr)oldSize=0;
  if(!newSize){std::free(ptr);m->allocated-=oldSize;return nullptr;}
  if(newSize>32u*1024u*1024u || m->allocated-oldSize>32u*1024u*1024u-newSize)return nullptr;
  void*out=std::realloc(ptr,newSize);if(out)m->allocated=m->allocated-oldSize+newSize;return out;
}
void Budget(lua_State*L,lua_Debug*) {
  auto*m=*static_cast<Module**>(lua_getextraspace(L));
  if(m->budget<=10000)luaL_error(L,"Native Bridge callback instruction budget exceeded");m->budget-=10000;
}
void SetBudget(Module&m){m.budget=2000000;lua_sethook(m.L,Budget,LUA_MASKCOUNT,10000);}
int SessionGeneration(lua_State* L){lua_pushinteger(L,Current(L)->host->ActiveGeneration());return 1;}
int SessionTick(lua_State* L){lua_pushinteger(L,Current(L)->host->ActiveTick());return 1;}
int SessionPost(lua_State* L) {
  auto* m=Current(L);
  if(lua_gettop(L)!=1||!lua_isfunction(L,1)||!m->host->InLuaContext()){lua_pushnil(L);lua_pushliteral(L,"expected_function_in_active_session");return 2;}
  lua_pushvalue(L,1);const int ref=luaL_ref(L,LUA_REGISTRYINDEX);
  const auto id=m->host->PostTask("lua."+m->id,m->host->ActiveGeneration(),[m,ref]{
    const int top=lua_gettop(m->L);SetBudget(*m);lua_rawgeti(m->L,LUA_REGISTRYINDEX,ref);
    if(lua_pcall(m->L,0,0,0)!=LUA_OK){++m->errors;m->host->Log("LUA_TASK_ERROR",m->id.c_str(),lua_tostring(m->L,-1));}
    lua_settop(m->L,top);
  },[m,ref]{luaL_unref(m->L,LUA_REGISTRYINDEX,ref);});
  if(!id){luaL_unref(L,LUA_REGISTRYINDEX,ref);lua_pushnil(L);lua_pushliteral(L,"no_session_or_task_queue_full");return 2;}
  lua_pushinteger(L,static_cast<lua_Integer>(id));return 1;
}
int SessionCancel(lua_State* L) {
  auto* m=Current(L);int ok=0;const auto id=lua_tointegerx(L,1,&ok);
  lua_pushboolean(L,lua_gettop(L)==1&&ok&&id>0&&m->host->InLuaContext()&&m->host->CancelTask("lua."+m->id,static_cast<std::uint64_t>(id)));return 1;
}
int ReadLua(lua_State*L,const char*filename,const Module* module=nullptr) {
  if(!filename){lua_pushliteral(L,"filename expected");return LUA_ERRFILE;}
  int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,filename,-1,nullptr,0);
  if(!n){lua_pushliteral(L,"invalid UTF-8 filename");return LUA_ERRFILE;}
  std::wstring path(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,filename,-1,path.data(),n);path.pop_back();
  if(module&&_wcsicmp(path.c_str(),module->sdkPath.c_str())!=0) {
    auto relative=std::filesystem::path(path).lexically_relative(std::filesystem::path(module->descriptor.rootPath)).generic_wstring();
    std::wstring resolved;
    if(relative.empty()||!host::ResolveModAssetPath(module->descriptor,relative,L".lua",&resolved)) {
      lua_pushliteral(L,"Lua file must remain inside this module's safe code root");return LUA_ERRFILE;
    }
    path=std::move(resolved);
  }
  std::ifstream file(std::filesystem::path(path),std::ios::binary|std::ios::ate);
  if(!file||file.tellg()<0||file.tellg()>8*1024*1024){lua_pushliteral(L,"cannot read Lua file or file exceeds 8 MiB");return LUA_ERRFILE;}
  std::string data(static_cast<std::size_t>(file.tellg()),'\0');file.seekg(0);if(!data.empty())file.read(data.data(),data.size());
  if(!file){lua_pushliteral(L,"Lua file read failed");return LUA_ERRFILE;}
  return luaL_loadbufferx(L,data.data(),data.size(),filename,"t");
}
int LoadFile(lua_State*L){int rc=ReadLua(L,lua_tostring(L,1),Current(L));if(rc==LUA_OK)return 1;lua_pushnil(L);lua_insert(L,-2);return 2;}
int DoFile(lua_State*L){int top=lua_gettop(L);int rc=ReadLua(L,lua_tostring(L,1),Current(L));if(rc!=LUA_OK)return lua_error(L);lua_call(L,0,LUA_MULTRET);return lua_gettop(L)-top;}
void OpenLibraries(lua_State* L) {
  const luaL_Reg libraries[]={{"_G",luaopen_base},{LUA_TABLIBNAME,luaopen_table},{LUA_STRLIBNAME,luaopen_string},
    {LUA_MATHLIBNAME,luaopen_math},{LUA_UTF8LIBNAME,luaopen_utf8},{LUA_COLIBNAME,luaopen_coroutine},{nullptr,nullptr}};
  for(const auto* library=libraries;library->name;++library){luaL_requiref(L,library->name,library->func,1);lua_pop(L,1);}
}
int VehicleRegister(lua_State* L) {
  auto* module=Current(L);
  if (!module || lua_gettop(L)!=1 || !lua_istable(L,1)) {
    lua_pushnil(L);lua_pushliteral(L,"expected_descriptor_table");return 2;
  }
  lua_getfield(L,1,"id");
  const char* value=lua_type(L,-1)==LUA_TSTRING?lua_tostring(L,-1):nullptr;
  const std::string id=value?value:"";
  lua_pop(L,1);
  const auto identity=module->host->ResolveEarlyVehicle(module->id,id);
  if (identity.empty()) {
    lua_pushnil(L);lua_pushliteral(L,"not_declared_in_early_phase");return 2;
  }
  lua_pushlstring(L,identity.data(),identity.size());return 1;
}
void PushVehicle(lua_State* L,const api::vehicle::DefinitionRecord& record) {
  const auto state=record.state.load(std::memory_order_acquire);
  lua_createtable(L,0,9);
  const auto identity=Utf8(record.descriptor.identity);
  lua_pushlstring(L,identity.data(),identity.size());lua_setfield(L,-2,"identity");
  lua_pushinteger(L,record.descriptor.definitionId);lua_setfield(L,-2,"definition_id");
  lua_pushinteger(L,record.descriptor.nativeBaseType);lua_setfield(L,-2,"base_type");
  lua_pushstring(L,record.displayName.c_str());lua_setfield(L,-2,"name");
  lua_pushboolean(L,(state&api::vehicle::NameRegistered)!=0);lua_setfield(L,-2,"name_registered");
  lua_pushboolean(L,(state&api::vehicle::ArrayInitialized)!=0);lua_setfield(L,-2,"array_initialized");
  lua_pushboolean(L,(state&api::vehicle::XmlParsed)!=0);lua_setfield(L,-2,"xml_parsed");
  lua_pushboolean(L,(state&api::vehicle::Ready)!=0);lua_setfield(L,-2,"ready");
}
int VehicleLookup(lua_State* L) {
  const char* identity=lua_type(L,1)==LUA_TSTRING?lua_tostring(L,1):nullptr;
  const auto* record=identity?api::vehicle::Registry().Find(Wide(identity)):nullptr;
  if (!record) {lua_pushnil(L);return 1;}
  PushVehicle(L,*record);return 1;
}
int VehicleList(lua_State* L) {
  auto& registry=api::vehicle::Registry();
  lua_createtable(L,static_cast<int>(registry.Count()),0);
  int output=1;
  for (std::uint32_t id=registry.FirstId();id<registry.Limit();++id) {
    const auto* record=registry.Find(id);
    if (!record) continue;
    PushVehicle(L,*record);lua_rawseti(L,-2,output++);
  }
  return 1;
}
int VehicleSpawnInHangar(lua_State* L) {
  auto* module=Current(L);
  auto fail=[&](const char* reason){lua_pushnil(L);lua_pushstring(L,reason);return 2;};
  if(!module||lua_gettop(L)!=3||lua_type(L,1)!=LUA_TSTRING)
    return fail("expected_type_identity_carrier_id_and_slot");

  std::size_t identitySize=0;
  const char* identityBytes=lua_tolstring(L,1,&identitySize);
  const auto identity=WideUtf8(identityBytes,identitySize);
  if(identity.empty())return fail("invalid_or_missing_type_identity");

  int carrierOk=0,slotOk=0;
  const auto carrier=lua_tointegerx(L,2,&carrierOk);
  const auto slot=lua_tointegerx(L,3,&slotOk);
  if(!carrierOk||!slotOk||carrier<=0||carrier>UINT32_MAX||slot<0||slot>UINT32_MAX)
    return fail("expected_uint32_carrier_id_and_slot");

  const auto* record=api::vehicle::Registry().Find(identity);
  if(!record)return fail("unknown_vehicle_type");
  if(!api::vehicle::Registry().ReadyFor(record->descriptor.definitionId))
    return fail("vehicle_type_not_ready");

  const auto requestId=module->host->QueueHangarSpawn(static_cast<std::uint32_t>(carrier),
      static_cast<std::uint32_t>(slot),record->descriptor.definitionId);
  if(!requestId)return fail("no_session_or_queue_busy");
  lua_pushinteger(L,requestId);return 1;
}
int Result(lua_State* L,bool ok,const char* reason) {lua_pushboolean(L,ok);lua_pushstring(L,reason);return 2;}
int Throttle(lua_State* L) {
  auto*m=Current(L);int ok=0;auto token=lua_tointegerx(L,1,&ok);
  return Result(L,ok&&token>=0&&token<=2&&m->host->QueueThrottle(static_cast<int>(token)-1),"queued_or_unavailable");
}
int Key(lua_State* L) {
  int ok=0;auto key=lua_tointegerx(L,1,&ok);
  lua_pushboolean(L,ok&&key>0&&key<256&&Current(L)->host->KeyDown(static_cast<int>(key)));return 1;
}
int Animation(lua_State* L) {
  int ok=0;auto multiplier=lua_tointegerx(L,1,&ok);
  auto group=static_cast<api::animation::Group>(lua_tointeger(L,lua_upvalueindex(2)));
  const auto* api=Current(L)->host->Api();
  return Result(L,ok&&multiplier>=1&&static_cast<unsigned long long>(multiplier)<=UINT32_MAX&&api->animation_speed&&
    api->animation_speed(static_cast<std::uint32_t>(group),static_cast<unsigned>(multiplier)),"applied_or_unavailable");
}
int HangarSpawn(lua_State* L) {
  auto* m=Current(L);
  if(!m||lua_gettop(L)!=3)return Result(L,false,"expected_carrier_id_slot_chassis_type");
  int carrierOk=0,slotOk=0,typeOk=0;
  const auto carrier=lua_tointegerx(L,1,&carrierOk);
  const auto slot=lua_tointegerx(L,2,&slotOk);
  const auto type=lua_tointegerx(L,3,&typeOk);
  if(!carrierOk||!slotOk||!typeOk||carrier<=0||carrier>UINT32_MAX||slot<0||slot>UINT32_MAX||type<0||type>UINT32_MAX)
    return Result(L,false,"expected_uint32_carrier_id_slot_and_chassis_type");
  const auto requestId=m->host->QueueHangarSpawn(static_cast<std::uint32_t>(carrier),
      static_cast<std::uint32_t>(slot),static_cast<std::uint32_t>(type));
  if(!requestId)return Result(L,false,"no_session_or_queue_busy");
  lua_pushboolean(L,1);lua_pushliteral(L,"queued");lua_pushinteger(L,requestId);return 3;
}
int BridgeHangarSpawnResult(lua_State* L) {
  auto* m=Current(L);int ok=0;auto id=lua_tointegerx(L,1,&ok);
  if(!m||lua_gettop(L)!=1||!ok||id<=0||id>UINT32_MAX) {
    lua_pushnil(L);lua_pushliteral(L,"request_id_must_be_positive_uint32");return 2;
  }
  Host::HangarSpawnResult result{};
  if(!m->host->FindHangarSpawnResult(static_cast<std::uint32_t>(id),&result)) {
    lua_pushnil(L);lua_pushliteral(L,"pending_or_expired");return 2;
  }
  lua_createtable(L,0,4);
  lua_pushinteger(L,result.requestId);lua_setfield(L,-2,"request_id");
  lua_pushboolean(L,result.success);lua_setfield(L,-2,"success");
  if(result.vehicleId){lua_pushinteger(L,result.vehicleId);lua_setfield(L,-2,"vehicle_id");}
  if(result.errorCode){lua_pushinteger(L,result.errorCode);lua_setfield(L,-2,"error_code");}
  return 1;
}
int HangarSlots(lua_State* L) {
  auto* m=Current(L);
  if(!m||!m->host->InLuaContext()) { lua_pushnil(L);lua_pushliteral(L,"wrong_execution_context_or_no_session");return 2; }
  if(lua_gettop(L)!=2) { lua_pushnil(L);lua_pushliteral(L,"expected_carrier_id_and_chassis_type");return 2; }
  int carrierOk=0,typeOk=0;
  const auto carrier=lua_tointegerx(L,1,&carrierOk);
  const auto type=lua_tointegerx(L,2,&typeOk);
  if(!carrierOk||!typeOk||carrier<=0||carrier>UINT32_MAX||type<0||type>UINT32_MAX) {
    lua_pushnil(L);lua_pushliteral(L,"expected_uint32_carrier_id_and_chassis_type");return 2;
  }
  std::vector<Host::HangarSlotOption> options;
  std::string reason;
  if(!m->host->DescribeHangarSlots(static_cast<std::uint32_t>(carrier),
      static_cast<std::uint32_t>(type),&options,&reason)) {
    lua_pushnil(L);lua_pushstring(L,reason.c_str());return 2;
  }
  lua_createtable(L,static_cast<int>(options.size()),0);
  for(std::size_t i=0;i<options.size();++i) {
    lua_createtable(L,0,3);
    lua_pushinteger(L,options[i].index);lua_setfield(L,-2,"index");
    lua_pushboolean(L,options[i].compatible);lua_setfield(L,-2,"compatible");
    lua_pushboolean(L,options[i].occupied);lua_setfield(L,-2,"occupied");
    lua_rawseti(L,-2,static_cast<lua_Integer>(i+1));
  }
  return 1;
}
const char* EntityTypeError(cc2x_entity_type_result result) {
  switch (result) {
    case CC2X_ENTITY_TYPE_DUPLICATE: return "type_id_already_registered";
    case CC2X_ENTITY_TYPE_FULL: return "registry_capacity_reached";
    case CC2X_ENTITY_TYPE_NOT_OWNER: return "not_type_owner";
    case CC2X_ENTITY_TYPE_INVALID: return "invalid_type_descriptor";
    default: return "registered";
  }
}
int EntityTypeRegister(lua_State* L) {
  auto* m=Current(L);
  if (!m || !m->host->InLuaContext()) return Result(L,false,"wrong_execution_context_or_no_session");
  if (lua_gettop(L)!=3 || lua_type(L,1)!=LUA_TSTRING || lua_type(L,2)!=LUA_TSTRING || lua_type(L,3)!=LUA_TSTRING)
    return Result(L,false,"expected_id_base_definition_strings");
  for(int i=1;i<=3;++i){std::size_t size=0;const auto* text=lua_tolstring(L,i,&size);
    if(!size||size>=97||std::memchr(text,0,size))return Result(L,false,"invalid_type_descriptor");}
  const auto result=cc2x_entity_type_register(m->id.c_str(),lua_tostring(L,1),lua_tostring(L,2),lua_tostring(L,3));
  return Result(L,result==CC2X_ENTITY_TYPE_OK,EntityTypeError(result));
}
int EntityTypeRemove(lua_State* L) {
  auto* m=Current(L);if(!m||!m->host->InLuaContext())return Result(L,false,"wrong_execution_context_or_no_session");
  std::size_t size=0;const auto* text=lua_type(L,1)==LUA_TSTRING?lua_tolstring(L,1,&size):nullptr;
  if(lua_gettop(L)!=1||!text||!size||size>=97||std::memchr(text,0,size))return Result(L,false,"invalid_type_id");
  return Result(L,cc2x_entity_type_remove(m->id.c_str(),text)!=0,"removed_or_not_owned");
}
int EntityTypeClear(lua_State* L) {
  auto* m=Current(L);if(!m||!m->host->InLuaContext())return Result(L,false,"wrong_execution_context_or_no_session");
  if(lua_gettop(L))return Result(L,false,"no_arguments_expected");
  lua_pushinteger(L,cc2x_entity_type_remove_owner(m->id.c_str()));return 1;
}
int EntityTypeCount(lua_State* L) {
  auto* m=Current(L);
  if (!m || !m->host->InLuaContext()) return Result(L,false,"wrong_execution_context_or_no_session");
  if (lua_gettop(L)!=0) return Result(L,false,"no_arguments_expected");
  lua_pushinteger(L,cc2x_entity_type_count());return 1;
}
int EntityTypes(lua_State* L) {
  auto* m=Current(L);
  if (!m || !m->host->InLuaContext()) return Result(L,false,"wrong_execution_context_or_no_session");
  if (lua_gettop(L)!=0) return Result(L,false,"no_arguments_expected");
  const int count=cc2x_entity_type_count();
  lua_createtable(L,count,0);
  for(int i=0;i<count;++i){
    cc2x_entity_type type{};if(!cc2x_entity_type_get(i,&type))continue;
    lua_createtable(L,0,4);
    lua_pushstring(L,type.id);lua_setfield(L,-2,"id");
    lua_pushstring(L,type.owner);lua_setfield(L,-2,"owner");
    lua_pushstring(L,type.base);lua_setfield(L,-2,"base");
    lua_pushstring(L,type.definition);lua_setfield(L,-2,"definition");
    lua_rawseti(L,-2,i+1);
  }
  return 1;
}
int Handler(lua_State* L) {
  auto*m=Current(L);auto token=static_cast<unsigned long long>(lua_tointeger(L,1));
  unsigned index=static_cast<unsigned>(token&0x7fffffffu);if(index>258)return 0;
  auto&counter=m->handlers[index];++counter.calls;++m->callbacks;
  if(token&0x80000000u) {
    ++m->errors;++counter.errors;
    if(counter.errors<=3||counter.errors%1000==0){char detail[2048];const char*err=lua_tostring(L,2);sprintf_s(detail,"handler=%u errors=%llu reason=%.1600s",index,counter.errors,err?err:"Lua callback error");m->host->Log("CALLBACK_ERROR",m->id.c_str(),detail);}
  }
  return 0;
}
int Print(lua_State* L) {
  auto*m=Current(L);std::string s;
  for(int i=1;i<=lua_gettop(L);++i) {size_t n=0;const char*v=lua_tolstring(L,i,&n);if(i>1)s+='\t';if(v)s.append(v,std::min<size_t>(n,2048));}
  m->host->Log("LUA_LOG",m->id.c_str(),s.c_str());return 0;
}
int Ready(lua_State* L){lua_pushboolean(L,Current(L)->host->InLuaContext());return 1;}
int One(lua_State* L){lua_pushinteger(L,1);return 1;}
int Version(lua_State* L){lua_pushstring(L,"cc2x-native-bridge-1");return 1;}
int Reload(lua_State* L){Current(L)->host->RequestReload();lua_pushboolean(L,1);return 1;}
int StockThrottle(cc2x_lua* L) {
  std::int64_t step=0;
  auto readInteger=g_stockLuaApi?g_stockLuaApi->lua_tointeger:nullptr;
  if(!g_stockLuaApi||!g_stockLuaApi->carrier_throttle||!readInteger||
     !readInteger(L,1,&step)||step<-1||step>1) {
    if(g_stockLuaApi&&g_stockLuaApi->lua_pushnil&&g_stockLuaApi->lua_pushstring) {
      g_stockLuaApi->lua_pushnil(L);g_stockLuaApi->lua_pushstring(L,"expected_step_minus1_zero_or_plus1");return 2;
    }
    return 0;
  }
  const int accepted=g_stockLuaApi->carrier_throttle(static_cast<int>(step));
  g_stockLuaApi->lua_pushboolean(L,accepted);
  g_stockLuaApi->lua_pushstring(L,accepted?"queued":"no_active_session_or_queue_full");
  return 2;
}
int StockHangarSpawn(cc2x_lua* L) {
  std::int64_t carrier=0,slot=0,chassis=0;
  auto* api=g_stockLuaApi;
  auto readInteger=api?api->lua_tointeger:nullptr;
  if(!api||!api->carrier_hangar_spawn||!readInteger||
     !readInteger(L,1,&carrier)||!readInteger(L,2,&slot)||!readInteger(L,3,&chassis)||
     carrier<=0||carrier>UINT32_MAX||slot<0||slot>UINT32_MAX||chassis<0||chassis>UINT32_MAX) {
    if(api&&api->lua_pushnil&&api->lua_pushstring) {
      api->lua_pushnil(L);api->lua_pushstring(L,"invalid_argument: expected carrier_id, slot, chassis_type");return 2;
    }
    return 0;
  }
  const int requestId=api->carrier_hangar_spawn(static_cast<std::uint32_t>(carrier),
      static_cast<std::uint32_t>(slot),static_cast<std::uint32_t>(chassis));
  if(!requestId) {
    api->lua_pushnil(L);api->lua_pushstring(L,"no_session_or_queue_busy");return 2;
  }
  api->lua_pushinteger(L,requestId);
  return 1;
}
int StockAnimationGroup(cc2x_lua* L,std::uint32_t group) {
  std::int64_t value=0;
  auto readInteger=g_stockLuaApi?g_stockLuaApi->lua_tointeger:nullptr;
  if(!g_stockLuaApi||!g_stockLuaApi->animation_speed||!readInteger||
     !readInteger(L,1,&value)||value<1||value>UINT32_MAX) {
    if(g_stockLuaApi&&g_stockLuaApi->lua_pushnil&&g_stockLuaApi->lua_pushstring) {
      const char* reason=!g_stockLuaApi->animation_speed?"unavailable":
          (!readInteger||!readInteger(L,1,&value))?"expected_integer_multiplier":
          "multiplier_out_of_range";
      g_stockLuaApi->lua_pushnil(L);g_stockLuaApi->lua_pushstring(L,reason);return 2;
    }
    return 0;
  }
  const int applied=g_stockLuaApi->animation_speed(group,static_cast<std::uint32_t>(value));
  g_stockLuaApi->lua_pushboolean(L,applied);
  g_stockLuaApi->lua_pushstring(L,applied?"applied":"not_ready_or_unsupported_value");
  return 2;
}
int StockRoutes(cc2x_lua*L){return StockAnimationGroup(L,CC2X_ANIMATION_ROUTES);}
int StockRunway(cc2x_lua*L){return StockAnimationGroup(L,CC2X_ANIMATION_RUNWAY);}
int StockLift(cc2x_lua*L){return StockAnimationGroup(L,CC2X_ANIMATION_LIFT);}
int StockCrane(cc2x_lua*L){return StockAnimationGroup(L,CC2X_ANIMATION_CRANE);}
void OnTick(void*s,void*u){static_cast<Host*>(u)->Tick(s);}
void OnSession(void*s,int start,void*u){static_cast<Host*>(u)->Session(s,start!=0);}
void OnHangarSpawnResult(std::uint32_t requestId,int success,std::uint32_t vehicleId,std::uint32_t errorCode,void* user) {
  static_cast<Host*>(user)->RecordHangarSpawnResult(requestId,success,vehicleId,errorCode);
}
}
Host::Host(bool(*sampler)(int)):inputSampler_(sampler){}
Host::~Host(){Shutdown();}
bool Host::Attach(const cc2x_api* api,bool installNative,bool deferCatalog) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(attached_)return api&&api_.module_base==api->module_base;
  if(!api||api->api_version<5||api->size<offsetof(cc2x_api,patch_bytes)+sizeof(api->patch_bytes))return false;
  std::memcpy(&api_,api,std::min<std::size_t>(api->size,sizeof(api_)));
  if(!api_.on_server_tick||!api_.on_session||!api_.active_mod_count||!api_.active_mod_path||!api_.log)return false;
  if(!api_.config_dir||!api_.config_dir[0])return false;
  nativeExtensionsReady_=!installNative||InstallNativeExtensions(&api_,this);
  if(!nativeExtensionsReady_) {
    api_.carrier_throttle=nullptr;
    api_.animation_speed=nullptr;
    api_.carrier_hangar_spawn=nullptr;
    api_.on_hangar_spawn_result=nullptr;
    if(api_.log)api_.log("native_bridge",CC2X_LOG_WARN,
      "event=NATIVE_FEATURES_DISABLED reason=game_build_addresses_not_verified; independent Lua runtime remains available");
  } else if(api_.game_build&&std::strcmp(api_.game_build,"1.5.18-win32-r2")==0) {
    // The instruction-level animation profile is installed transactionally
    // by NativeExtensions. Hangar calls have their own exact-hash, 19-entry
    // profile and are exposed only after its signatures validate.
    std::string hangarReason;
    const bool hangarReady=ValidateHangarSpawnImage(&api_,&hangarReason);
    if(!hangarReady) {
      api_.carrier_hangar_spawn=nullptr;
      api_.on_hangar_spawn_result=nullptr;
      if(api_.log)api_.log("native_bridge",CC2X_LOG_WARN,
        "event=HANGAR_PROFILE_UNAVAILABLE build=%s reason=%s",api_.game_build,hangarReason.c_str());
    } else if(api_.log) {
      api_.log("native_bridge",CC2X_LOG_INFO,
        "event=HANGAR_PROFILE_READY build=%s functions=19 sha256_and_entry_guards=verified",api_.game_build);
    }
    const bool animationReady=api::animation::IsReady();
    const bool hangarReadyForCalls=api_.carrier_hangar_spawn!=nullptr;
    if(api_.log)api_.log("native_bridge",animationReady&&hangarReadyForCalls?CC2X_LOG_INFO:CC2X_LOG_WARN,
      "event=%s build=%s throttle=ready game_lua=ready world=ready animation=%s hangar_spawn=%s",
      animationReady&&hangarReadyForCalls?"NATIVE_FEATURES_READY":"NATIVE_FEATURES_PARTIAL",
      api_.game_build,animationReady?"ready":"unsupported",hangarReadyForCalls?"ready":"unsupported");
  }
  g_stockLuaApi=&api_;
  g_stockHost=this;
  if(api_.lua_add&&api_.lua_gettop&&api_.lua_type&&api_.lua_tostring&&api_.lua_tointeger&&api_.lua_tonumber&&api_.lua_pushboolean&&api_.lua_pushnil&&api_.lua_pushnumber) {
    const bool messages=api_.lua_add("modules","send",StockMessageSend)&&api_.lua_add("modules","read",StockMessageRead);
    if(messages&&api_.lua_feature)api_.lua_feature("module_messages");
  }
  bool luaCarrier=false,luaAnimation=false,luaHangar=false;
  const bool luaVehicleDefinitions=api::vehicle::RegisterStockLuaApi(&api_);
  if(api_.log)api_.log("native_bridge",luaVehicleDefinitions?CC2X_LOG_INFO:CC2X_LOG_WARN,
      "event=VEHICLE_DEFINITION_LUA_API %s",luaVehicleDefinitions?"registered":"registration_failed");
  const bool luaVehicleUiCompatibility=api::vehicle::RegisterStockLuaCompatibilityApi(&api_);
  if(api_.log)api_.log("native_bridge",luaVehicleUiCompatibility?CC2X_LOG_INFO:CC2X_LOG_WARN,
      "event=VEHICLE_UI_COMPATIBILITY_API %s",luaVehicleUiCompatibility?"registered":"unavailable_or_registration_failed");
  if(api_.lua_add&&api_.carrier_throttle)luaCarrier=api_.lua_add("carrier","throttle",StockThrottle)!=0;
  if(api_.lua_add&&api_.carrier_hangar_spawn)luaHangar=api_.lua_add(nullptr,"spawn_in_hangar",StockHangarSpawn)!=0;
  if(api_.on_hangar_spawn_result&&!api_.on_hangar_spawn_result(OnHangarSpawnResult,this))
    api_.log("native_bridge",CC2X_LOG_WARN,"event=HANGAR_RESULT_SUBSCRIBE_FAILED Bridge result polling unavailable");
  if(api_.lua_add&&api_.animation_speed&&(!installNative||api::animation::IsReady())) {
    luaAnimation=api_.lua_add("animation","routes",StockRoutes)!=0;
    luaAnimation=api_.lua_add("animation","runway",StockRunway)!=0&&luaAnimation;
    luaAnimation=api_.lua_add("animation","lift",StockLift)!=0&&luaAnimation;
    luaAnimation=api_.lua_add("animation","crane",StockCrane)!=0&&luaAnimation;
  }
  if(luaCarrier&&api_.lua_feature)api_.lua_feature("carrier_control");
  if(luaHangar&&api_.lua_feature)api_.lua_feature("carrier_hangar_spawn");
  if(luaAnimation&&api_.lua_feature)api_.lua_feature("animation_speed");
  if(api_.log)api_.log("native_bridge",CC2X_LOG_INFO,"event=CC2X_API6_LUA_REGISTERED carrier=%d animation=%d",luaCarrier,luaAnimation);
  if(api_.log)api_.log("native_bridge",CC2X_LOG_INFO,"event=CC2X_API6_LUA_REGISTERED hangar_spawn=%d",luaHangar);
  storage_=Wide(api_.config_dir)+L"\\native_bridge";
  commands_.Enable(true);attached_=true;
  // CreateThread is deliberately not joined here: Attach runs during the
  // proxy's DLL_PROCESS_ATTACH. The worker body begins after loader-lock
  // serialization and reads the completed CC2X activation snapshot.
  if (!installNative) {
    // Pure host tests do not run inside DLL_PROCESS_ATTACH; prepare directly
    // so their first synthetic server tick observes the same published catalog.
    if(!deferCatalog)PrepareStartupCatalog();
  } else {
    HANDLE catalogThread = CreateThread(nullptr, 0, PrepareStartupCatalogThread,
        this, 0, nullptr);
    if (catalogThread) CloseHandle(catalogThread);
    else {
      InterlockedExchange(&startupCatalogState_, -1);
      Log("STARTUP_CATALOG_THREAD_FAILED", "host");
    }
  }
  api_.on_session(OnSession,this);api_.on_server_tick(OnTick,this);
  eventCapabilities_["waypoints"]=api_.on_waypoints&&api_.on_waypoints(WorldWaypoints,this);
  eventCapabilities_["airfield_tick"]=api_.on_airfield_tick&&api_.on_airfield_tick(WorldAirfield,this);
  eventCapabilities_["hangar_choice"]=api_.on_hangar_choice&&api_.on_hangar_choice(WorldHangar,this);
  eventCapabilities_["island_controller"]=api_.on_island_controller&&api_.on_island_controller(WorldController,this);
  eventCapabilities_["island_spawn"]=api_.on_island_spawn&&api_.on_island_spawn(WorldSpawn,this);
  eventCapabilities_["vehicle_controls"]=api::vehicle::ButtonsSupported();
  eventCapabilities_["vehicle_control_query"]=api::vehicle::ButtonsSupported();
  Log("PLUGIN_ATTACHED","host","API size/version checked; two stable native subscriptions; VM deferred");return true;
}
void Host::Log(const char* event,const char* mod,const char* detail) {
  if(api_.log)api_.log("native_bridge",CC2X_LOG_INFO,"event=%s mod=%s generation=%llu vm_generation=%llu thread=%lu %s",event,mod,
    metrics_.generation,metrics_.vmGeneration,GetCurrentThreadId(),detail);
}
void Host::RecordWorldRead(const char* operation,const char* error,unsigned long long microseconds,unsigned long long objects,bool success,bool wrongThread) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  ++worldReads_.calls;
  if(!success){++worldReads_.failures;if(error&&worldReads_.loggedErrors<3){char detail[256];sprintf_s(detail,"operation=%s reason=%s",operation?operation:"unknown",error);Log("WORLD_API_READ_ERROR","host",detail);++worldReads_.loggedErrors;}}
  if(wrongThread)++worldReads_.wrongThread;
  worldReads_.objects+=objects;
  worldReads_.microseconds+=microseconds;
  if(microseconds>worldReads_.maxMicroseconds)worldReads_.maxMicroseconds=microseconds;
}
void Host::Session(void* server,bool started) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(!attached_||stopped_)return;
  if(started) {
    if(server_==server)return;
    if(server_) {
      // A new server object is a new session even if the prior end callback
      // was missed. Complete, rather than silently lose, any queued request.
      if(hangarSpawnRequested_) {
        hangarSpawnResultSuccess_=false;hangarSpawnResultVehicleId_=0;
        hangarSpawnResultError_=CC2X_HANGAR_SPAWN_SESSION_CHANGED;
        CompleteHangarSpawn();
        LogHangarSpawn("HANGAR_SPAWN_DROPPED","new server replaced session before execution");
      }
      hangarSpawnRequested_=false;hangarSpawnInFlight_=false;
      metrics_.dropped+=commands_.size();commands_.End(commands_.active);
    }
    hangarSpawnResults_.clear(); // Results belong to the world that produced them.
    server_=server;++metrics_.generation;threadGeneration_=0;lastTick_=0;sessionTicks_=0;
    mailbox_.Begin(metrics_.generation);
    hangarSpawnRequested_=false;hangarSpawnInFlight_=false;hangarSpawnUnsafeLatched_=false;
    hangarSpawnRollbackPending_=false;
    hangarSpawnRequestGeneration_=hangarSpawnAttemptGeneration_=metrics_.generation;
    hangarSpawnRollbackId_=0;hangarSpawnRollbackTicks_=0;
    hangarSpawnCarrierId_=hangarSpawnSlot_=hangarSpawnType_=0;
    commands_.Begin({reinterpret_cast<std::uintptr_t>(server),metrics_.generation});
    char detail[160];sprintf_s(detail,"server=%p Lua execution deferred to server tick",server);
    Log("SESSION_START","host",detail);
  } else if(server_==server) {
    if(hangarSpawnRequested_) {
      hangarSpawnResultSuccess_=false;hangarSpawnResultVehicleId_=0;
      hangarSpawnResultError_=CC2X_HANGAR_SPAWN_SESSION_CHANGED;
      CompleteHangarSpawn();
      LogHangarSpawn("HANGAR_SPAWN_DROPPED","session ended before server-thread execution");
    }
    hangarSpawnResults_.clear();
    mailbox_.End();
    hangarSpawnRequested_=false;hangarSpawnInFlight_=false;hangarSpawnUnsafeLatched_=false;
    hangarSpawnRollbackPending_=false;hangarSpawnRollbackId_=0;hangarSpawnRollbackTicks_=0;
    hangarSpawnCarrierId_=hangarSpawnSlot_=hangarSpawnType_=0;
    auto dropped=commands_.size();metrics_.dropped+=dropped;commands_.End(commands_.active);server_=nullptr;lastTick_=0;sessionTicks_=0;
    char detail[160];sprintf_s(detail,"server=%p dropped=%zu queued commands invalidated; VMs retained",server,dropped);
    Log("SESSION_END","host",detail);
  }
}
bool Host::InLuaContext() const {return inLua_&&thread_==GetCurrentThreadId()&&server_&&!stopped_;}
bool Host::KeyDown(int key) const {
  if(!InServerContext()||key<1||key>255)return false;
  if(inputSampler_)return inputSampler_(key);
  HWND window=GetForegroundWindow();DWORD pid=0;if(window)GetWindowThreadProcessId(window,&pid);
  return pid==GetCurrentProcessId()&&(GetAsyncKeyState(key)&0x8000)!=0;
}
bool Host::QueueThrottle(int step) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(!attached_||stopped_||!nativeExtensionsReady_||!server_||step<-1||step>1)return false;
  if(step==0)return true;
  auto r=commands_.Push(step,commands_.active);
  if(r==lifetime::EnqueueResult::Queued){++metrics_.requested;return true;}return false;
}
void Host::AfterCarrierUpdate(void* carrier) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(!server_||!carrier||thread_!=GetCurrentThreadId())return;
  std::int8_t steps[lifetime::EpochQueue::Capacity]{};auto n=commands_.Drain(commands_.active,steps);if(!n)return;
  auto*field=reinterpret_cast<float*>(static_cast<unsigned char*>(carrier)+0xC94);
  float before=*field,after=before;
  if(!api::carrier::ApplyThrottleSteps(before,steps,n,&after)){metrics_.dropped+=n;return;}
  *field=after;metrics_.applied+=n;
  char detail[160];sprintf_s(detail,"object=%p steps=%zu before=%.3f after=%.3f",carrier,n,before,after);
  Log("THROTTLE_APPLIED","host",detail);
}
void Host::Register(Module& m) {
  auto add=[&](const char*name,lua_CFunction fn){lua_pushlightuserdata(m.L,&m);lua_pushcclosure(m.L,fn,1);lua_setglobal(m.L,name);};
  add("cc2_bridge_event_subscription",EventSubscribe);
  if(m.rules) {
    add("print",Print);add("loadfile",LoadFile);add("dofile",DoFile);
    lua_pushinteger(m.L,2);lua_setglobal(m.L,"cc2_bridge_lua_api_version");
    lua_pushstring(m.L,m.id.c_str());lua_setglobal(m.L,"cc2_bridge_module_id");
    lua_newtable(m.L);
    for(const auto& capability:eventCapabilities_){
      lua_pushboolean(m.L,capability.second&&(capability.first=="island_controller"||capability.first=="island_spawn"));
      lua_setfield(m.L,-2,capability.first.c_str());
    }
    lua_setglobal(m.L,"cc2_bridge_event_capabilities");
    return; // No world, input, mutations, registry, messages or native loader.
  }
  add("cc2_bridge_apply_throttle_input",Throttle);add("cc2_bridge_key_down",Key);
  add("cc2_bridge_host_handler_event",Handler);add("print",Print);add("cc2_bridge_is_ready",Ready);
  add("loadfile",LoadFile);add("dofile",DoFile);
  add("cc2_bridge_abi_version",One);add("cc2_bridge_bootstrap_status",One);add("cc2_bridge_build_id",Version);
  add("cc2_bridge_reload_mods",Reload);
  add("cc2_bridge_messages_poll",MessagePoll);
  add("cc2_bridge_messages_publish",MessagePublish);
  add("cc2_bridge_messages_forget",MessageForget);
  add("cc2_bridge_messages_send",MessageSend);
  add("cc2_bridge_messages_read",MessageRead);
  add("cc2_bridge_session_generation",SessionGeneration);add("cc2_bridge_session_tick",SessionTick);
  add("cc2_bridge_session_post",SessionPost);add("cc2_bridge_session_cancel",SessionCancel);
  lua_pushstring(m.L,m.id.c_str());lua_setglobal(m.L,"cc2_bridge_module_id");
  lua_pushinteger(m.L,2);lua_setglobal(m.L,"cc2_bridge_lua_api_version");
  lua_newtable(m.L);for(const auto& capability:eventCapabilities_){lua_pushboolean(m.L,capability.second&&capability.first!="island_controller"&&capability.first!="island_spawn");lua_setfield(m.L,-2,capability.first.c_str());}
  lua_setglobal(m.L,"cc2_bridge_event_capabilities");
  add("cc2_bridge_hangar_spawn",HangarSpawn);
  add("cc2_bridge_hangar_spawn_result",BridgeHangarSpawnResult);
  add("cc2_bridge_vehicle_spawn_in_hangar",VehicleSpawnInHangar);
  add("cc2_bridge_hangar_slots",HangarSlots);
  add("cc2_bridge_entity_type_register",EntityTypeRegister);
  add("cc2_bridge_entity_type_remove",EntityTypeRemove);
  add("cc2_bridge_entity_type_clear",EntityTypeClear);
  add("cc2_bridge_vehicle_register",VehicleRegister);
  add("cc2_bridge_vehicle_lookup",VehicleLookup);
  add("cc2_bridge_vehicle_list",VehicleList);
  add("cc2_bridge_entity_type_count",EntityTypeCount);
  add("cc2_bridge_entity_types",EntityTypes);
  const char* names[]={"cc2_bridge_anim_routes","cc2_bridge_anim_runway","cc2_bridge_anim_lift","cc2_bridge_anim_crane"};
  for(int i=0;i<4;++i){lua_pushlightuserdata(m.L,&m);lua_pushinteger(m.L,i);lua_pushcclosure(m.L,Animation,2);lua_setglobal(m.L,names[i]);}
  RegisterWorld(m.L,this);
  RegisterNativeBindings(m);
  char worldDetail[256];
  sprintf_s(worldDetail,"sdk=%s api=%u live=%s physics=%s",api_.sdk_version?api_.sdk_version:"unknown",api_.api_version,
    api_.find&&api_.count&&api_.ref&&api_.geti&&api_.getf?"available":"partial",
    api_.body_get?"available":"unavailable");
  Log("WORLD_API_REGISTERED",m.id.c_str(),worldDetail);
}
bool Host::Call(Module& m,const char* name,double dt,bool argument) {
  SetBudget(m);
  int top=lua_gettop(m.L);lua_getglobal(m.L,name);
  if(!lua_isfunction(m.L,-1)){lua_settop(m.L,top);return false;}
  if(argument)lua_pushnumber(m.L,dt);
  int status=lua_pcall(m.L,argument?1:0,0,0);
  if(status!=LUA_OK){++m.errors;const char*err=lua_tostring(m.L,-1);Log("LUA_ERROR",m.id.c_str(),err?err:"unknown");}
  lua_settop(m.L,top);return status==LUA_OK;
}
int Host::WorldEvent(const char* name,double a,double b,double c,int neutral,bool decision) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(!attached_||stopped_||inLua_||!name||!eventCapabilities_[name])return neutral;
  const bool construction=std::strcmp(name,"island_controller")==0||std::strcmp(name,"island_spawn")==0;
  if(!server_&&!construction)return neutral;
  const DWORD current=GetCurrentThreadId();
  if(!construction&&thread_&&threadGeneration_==metrics_.generation&&thread_!=current){++metrics_.wrongThread;return neutral;}
  if(!construction){thread_=current;threadGeneration_=metrics_.generation;}
  inLua_=true;
  if(construction)LoadRules();else if(!loaded_)Load();
  int chosen=neutral;bool hasChoice=false,conflict=false;
  const auto nativeListeners=nativeListeners_; // callbacks may unsubscribe without invalidating iteration
  for(const auto& listener:nativeListeners) {
    if(listener.event!=name||std::none_of(nativeListeners_.begin(),nativeListeners_.end(),[&](const auto& active){return active.id==listener.id;}))continue;
    const cc2x_framework_event event{sizeof(cc2x_framework_event),name,Generation(),SessionTickId(),a,b,c};
    NativeEventCall call{listener.callback,&event,listener.user};
    if(!cc2x_seh_guard(CallNativeEvent,&call)) {if(buttonsContext_)buttonsContext_->rejected=true;Log("NATIVE_EVENT_ERROR","framework",name);continue;}
    if(!decision||call.result==CC2X_FRAMEWORK_DEFAULT)continue;
    const auto result=call.result;
    const bool valid=std::strcmp(name,"hangar_choice")==0?result>=-1&&result<=63:
      std::strcmp(name,"vehicle_control_query")==0?result>=-1&&result<=1023:result>=0&&result<=1;
    if(!valid){if(buttonsContext_)buttonsContext_->rejected=true;Log("INVALID_NATIVE_EVENT_DECISION","framework",name);continue;}
    if(result!=neutral){if(hasChoice&&chosen!=result)conflict=true;chosen=result;hasChoice=true;}
  }
  auto& listeners=construction?ruleModules_:modules_;
  for(auto& module:listeners) {
    auto& m=*module;SetBudget(m);const int top=lua_gettop(m.L);lua_getglobal(m.L,"__cc2_engine_event");
    if(!lua_isfunction(m.L,-1)){lua_settop(m.L,top);continue;}
    lua_pushstring(m.L,name);lua_pushnumber(m.L,a);lua_pushnumber(m.L,b);lua_pushnumber(m.L,c);
    const int status=lua_pcall(m.L,4,1,0);
    if(status!=LUA_OK){++m.errors;if(buttonsContext_)buttonsContext_->rejected=true;Log("LUA_EVENT_ERROR",m.id.c_str(),lua_tostring(m.L,-1));}
    else if(decision&&!lua_isnil(m.L,-1)) {
      int ok=0;auto result=lua_tointegerx(m.L,-1,&ok);
      if(lua_isboolean(m.L,-1)&&construction){ok=1;result=lua_toboolean(m.L,-1);}
      const bool valid=ok&&(std::strcmp(name,"hangar_choice")==0?result>=-1&&result<=63:
        std::strcmp(name,"vehicle_control_query")==0?result>=-1&&result<=1023:result>=0&&result<=1);
      if(!valid){++m.errors;Log("INVALID_EVENT_DECISION",m.id.c_str(),name);}
      else if(result!=neutral){if(hasChoice&&chosen!=result)conflict=true;chosen=static_cast<int>(result);hasChoice=true;}
    }
    lua_settop(m.L,top);
  }
  inLua_=false;
  if(conflict){Log("EVENT_DECISION_CONFLICT","host",name);return neutral;}
  return chosen;
}
void Host::LoadRules() {
  if(rulesLoaded_)return;
  if(InterlockedCompareExchange(&startupCatalogState_,0,0)!=1) {
    Log("RULE_CATALOG_UNAVAILABLE","host","construction decision retains vanilla behavior");return;
  }
  rulesLoaded_=true;
  std::error_code error;std::filesystem::create_directories(storage_,error);
  const auto sdk=storage_+L"\\cc2.lua";
  {std::ofstream file(std::filesystem::path(sdk),std::ios::binary|std::ios::trunc);file<<kBridgeSdk;if(!file){Log("SDK_WRITE_ERROR","rules");return;}}
  for(const auto& original:startupCatalog_.mods) {
    if(original.rulesEntry.empty())continue;
    auto mod=original;mod.entryPath=mod.rootPath+L"\\"+mod.rulesEntry;mod.bridgeScripts={mod.rulesEntry};mod.hooks.clear();
    std::wstring bootstrap,reason;
    if(!host::WriteBridgeBootstrap(mod,sdk,storage_+L"\\rules",&bootstrap,&reason)){Log("RULE_BOOTSTRAP_ERROR",Utf8(mod.id).c_str(),Utf8(reason).c_str());continue;}
    auto m=std::make_unique<Module>();m->host=this;m->id=Utf8(mod.id);m->descriptor=mod;m->sdkPath=sdk;m->rules=true;m->generation=++metrics_.vmGeneration;
    m->L=lua_newstate(Allocate,m.get());if(!m->L){Log("RULE_ALLOCATION_ERROR",m->id.c_str());continue;}
    *static_cast<Module**>(lua_getextraspace(m->L))=m.get();SetBudget(*m);OpenLibraries(m->L);Register(*m);
    auto path=Utf8(bootstrap);int rc=ReadLua(m->L,path.c_str());if(rc==LUA_OK)rc=lua_pcall(m->L,0,0,0);
    if(rc!=LUA_OK){Log("RULE_LOAD_ERROR",m->id.c_str(),lua_tostring(m->L,-1));lua_close(m->L);continue;}
    Log("RULE_MODULE_LOADED",m->id.c_str(),"no world/native/OS capabilities; engine decision callbacks only");ruleModules_.push_back(std::move(m));
  }
}
void Host::ButtonsEvent(api::vehicle::ButtonContext* context) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(!context||buttonsContext_)return;
  buttonsContext_=context;WorldEvent("vehicle_controls",context->vehicleId);buttonsContext_=nullptr;
}
int Host::ButtonQueryEvent(api::vehicle::ButtonContext* context) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);if(!context||buttonsContext_)return -1;
  buttonsContext_=context;const int result=WorldEvent("vehicle_control_query",context->vehicleId,context->sourceIndex,0,-1,true);buttonsContext_=nullptr;return result;
}
bool Host::EventAvailable(const std::string& name,bool rules) const {
  auto capability=eventCapabilities_.find(name);const bool construction=name=="island_controller"||name=="island_spawn";
  return capability!=eventCapabilities_.end()&&capability->second&&rules==construction;
}
bool Host::HasEventSubscribers(const char* name) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);if(stopped_||!name)return false;
  for(const auto& listener:nativeListeners_)if(listener.event==name)return true;
  for(const auto& m:modules_)if(m->subscriptions.count(name))return true;return false;
}
std::uint64_t Host::SubscribeNative(const std::string& event,cc2x_framework_event_fn callback,void* user) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);auto capability=eventCapabilities_.find(event);
  if(stopped_||!callback||nativeListeners_.size()>=256||capability==eventCapabilities_.end()||!capability->second||nextNativeId_>=INT64_MAX)return 0;
  if((event=="vehicle_controls"||event=="vehicle_control_query")&&!api::vehicle::EnsureButtons())return 0;
  const auto id=++nextNativeId_;nativeListeners_.push_back({id,event,callback,user});return id;
}
bool Host::UnsubscribeNative(std::uint64_t id) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto it=std::find_if(nativeListeners_.begin(),nativeListeners_.end(),[&](const auto& entry){return entry.id==id;});
  if(it==nativeListeners_.end())return false;nativeListeners_.erase(it);return true;
}
void Host::RegisterNativeBindings(Module& m) {
  lua_newtable(m.L);
  for(const auto& binding:nativeBindings_) {
    lua_getfield(m.L,-1,binding.owner.c_str());
    if(!lua_istable(m.L,-1)){lua_pop(m.L,1);lua_newtable(m.L);lua_pushvalue(m.L,-1);lua_setfield(m.L,-3,binding.owner.c_str());}
    lua_pushlightuserdata(m.L,&m);lua_pushinteger(m.L,static_cast<lua_Integer>(binding.id));lua_pushcclosure(m.L,BoundNative,2);
    lua_setfield(m.L,-2,binding.name.c_str());lua_pop(m.L,1);
  }
  lua_setglobal(m.L,"cc2_bridge_native");
}
std::uint64_t Host::BindNative(const std::string& owner,const std::string& name,cc2x_vm_fn callback,void* user) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(stopped_||!callback||!FrameworkName(owner)||!FrameworkName(name)||nativeBindings_.size()>=256||nextNativeId_>=INT64_MAX||
    !modules_.empty()||!ruleModules_.empty()||metrics_.updates||std::any_of(nativeBindings_.begin(),nativeBindings_.end(),[&](const auto& b){return b.owner==owner&&b.name==name;}))return 0;
  const auto id=++nextNativeId_;nativeBindings_.push_back({id,owner,name,callback,user});return id;
}
bool Host::UnbindNative(std::uint64_t id) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto it=std::find_if(nativeBindings_.begin(),nativeBindings_.end(),[&](const auto& b){return b.id==id;});
  if(it==nativeBindings_.end())return false;nativeBindings_.erase(it);return true;
}
bool Host::FindNativeBinding(std::uint64_t id,cc2x_vm_fn* callback,void** user) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);if(stopped_||!InLuaContext()||!callback||!user)return false;
  for(const auto& b:nativeBindings_)if(b.id==id){*callback=b.callback;*user=b.user;return true;}return false;
}
std::uint64_t Host::OpenNativeMailbox(const std::string& owner) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(stopped_||owner.rfind("native.",0)!=0||!FrameworkName(owner,128)||nativeMailboxes_.size()>=256||nextNativeId_>=INT64_MAX||!mailbox_.Enable(owner))return 0;
  const auto id=++nextNativeId_;nativeMailboxes_.emplace(id,owner);return id;
}
bool Host::CloseNativeMailbox(std::uint64_t id) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);auto it=nativeMailboxes_.find(id);if(it==nativeMailboxes_.end())return false;
  mailbox_.Remove(it->second);nativeMailboxes_.erase(it);return true;
}
int Host::PollNativeMailbox(std::uint64_t id,cc2x_message* out,std::uint32_t capacity) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);auto it=nativeMailboxes_.find(id);
  if(stopped_||!InServerContext()||it==nativeMailboxes_.end()||!out||capacity<64)return -1;
  auto messages=mailbox_.Drain(it->second);int count=0;
  for(const auto& message:messages){auto& record=out[count++];std::memset(&record,0,sizeof(record));std::memcpy(record.topic,message.topic.data(),message.topic.size());record.subject=message.subject;record.value=message.value;}
  return count;
}
bool Host::PublishNativeMailbox(std::uint64_t id,const std::string& topic,std::uint32_t subject,double value) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);auto it=nativeMailboxes_.find(id);
  return !stopped_&&InServerContext()&&it!=nativeMailboxes_.end()&&mailbox_.Publish(it->second,topic,subject,value);
}
bool Host::ForgetNativeMailbox(std::uint64_t id,std::uint32_t subject) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);auto it=nativeMailboxes_.find(id);
  if(stopped_||!InServerContext()||it==nativeMailboxes_.end())return false;mailbox_.Forget(it->second,subject);return true;
}
bool Host::RegisterNativeEntity(const std::string& owner,const char* id,const char* base,const char* definition) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(!InServerContext()||owner.rfind("native.",0)!=0||!FrameworkName(owner,96)||
     cc2x_entity_type_register(owner.c_str(),id,base,definition)!=CC2X_ENTITY_TYPE_OK)return false;
  if(std::find(nativeEntityOwners_.begin(),nativeEntityOwners_.end(),owner)==nativeEntityOwners_.end())nativeEntityOwners_.push_back(owner);
  return true;
}
unsigned long long Host::ActiveGeneration(){std::lock_guard<std::recursive_mutex> lock(mutex_);return server_&&!stopped_?metrics_.generation:0;}
unsigned long long Host::ActiveTick(){std::lock_guard<std::recursive_mutex> lock(mutex_);return server_&&!stopped_?sessionTicks_:0;}
std::uint64_t Host::PostTask(const std::string& owner,std::uint64_t generation,std::function<void()> run,std::function<void()> cleanup) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(!attached_||stopped_||!server_||!generation||generation!=metrics_.generation||!FrameworkName(owner,128)||!run||tasks_.size()>=256||nextNativeId_>=INT64_MAX||
    std::count_if(tasks_.begin(),tasks_.end(),[&](const auto& task){return task.owner==owner;})>=64)return 0;
  const auto id=++nextNativeId_;tasks_.push_back({id,generation,owner,std::move(run),std::move(cleanup)});return id;
}
bool Host::CancelTask(const std::string& owner,std::uint64_t id) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto it=std::find_if(tasks_.begin(),tasks_.end(),[&](const auto& task){return task.id==id&&task.owner==owner;});
  if(it==tasks_.end())return false;auto cleanup=std::move(it->cleanup);tasks_.erase(it);if(cleanup)cleanup();return true;
}
void Host::DropTasks(const std::string& owner) {
  std::vector<std::uint64_t> ids;for(const auto& task:tasks_)if(task.owner==owner)ids.push_back(task.id);
  for(auto id:ids)CancelTask(owner,id);
}
void Host::RunTasks(std::uint64_t cutoff) {
  std::vector<std::uint64_t> ids;for(const auto& task:tasks_)if(task.id<=cutoff)ids.push_back(task.id);
  for(auto id:ids){auto it=std::find_if(tasks_.begin(),tasks_.end(),[&](const auto& task){return task.id==id;});
    if(it==tasks_.end())continue;auto task=std::move(*it);tasks_.erase(it);
    if(task.generation==metrics_.generation)task.run();else{++metrics_.dropped;Log("STALE_TASK_DROPPED",task.owner.c_str());}
    if(task.cleanup)task.cleanup();
  }
}
bool Host::ReadVehicleThrottle(std::uint32_t id,double* value) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);if(!id||!value||!InServerContext()||!api_.find)return false;
  void* vehicle=api_.find(CC2X_WORLD_VEHICLES,id);float output=0;
  if(!vehicle||!api::vehicle::ReadThrottle(&api_,vehicle,&output))return false;*value=output;return true;
}
bool Host::WriteVehicleThrottle(std::uint32_t id,double value) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);if(!id||!InServerContext()||!api_.find)return false;
  void* vehicle=api_.find(CC2X_WORLD_VEHICLES,id);return vehicle&&api::vehicle::WriteThrottle(&api_,vehicle,value);
}
bool Host::DescribeNativePackage(const std::string& owner,const std::wstring& pluginDirectory,host::ModDescriptor* package) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(!package||stopped_||!attached_||nativeDeclarationsFrozen_||InterlockedCompareExchange(&resourceInitObserved_,0,0)||
    !FrameworkName(owner)||pluginDirectory.empty()||!api_.active_mod_count||!api_.active_mod_path)return false;
  const auto directory=std::filesystem::path(pluginDirectory).lexically_normal();
  auto safeDirectory=[](std::filesystem::path path){
    for(;;){auto attributes=GetFileAttributesW(path.c_str());if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT)||!(attributes&FILE_ATTRIBUTE_DIRECTORY))return false;
      auto parent=path.parent_path();if(parent.empty()||parent==path)break;path=std::move(parent);
    }return true;
  };
  if(!safeDirectory(directory))return false;
  const int count=api_.active_mod_count();if(count<0||count>256)return false;
  for(int i=0;i<count;++i){wchar_t path[1024]{};if(!api_.active_mod_path(i,path,1024))return false;
    const auto root=std::filesystem::path(path).lexically_normal();
    const auto standard=root/L"content"/L"cc2x",legacy=root/L"cc2x";
    if(_wcsicmp(directory.c_str(),standard.c_str())&&_wcsicmp(directory.c_str(),legacy.c_str()))continue;
    auto content=root/L"content";const DWORD attributes=GetFileAttributesW(content.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES)content=root;
    if(!safeDirectory(root)||!safeDirectory(content))return false;
    host::ModDescriptor result;result.id=Wide(owner.c_str());
    std::transform(result.id.begin(),result.id.end(),result.id.begin(),towlower);
    result.name=result.id;result.version=L"native";result.rootPath=content.wstring();result.packagePath=root.wstring();
    *package=std::move(result);return true;
  }return false;
}
bool Host::CommitNativeDeclarations(const std::vector<host::ModDescriptor>& packages,const std::vector<host::VehicleDeclaration>& declarations) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(stopped_||!attached_||nativeDeclarationsFrozen_||InterlockedCompareExchange(&resourceInitObserved_,0,0)||
    packages.size()>256||nativeDeclarations_.size()+declarations.size()>256)return false;
  auto combined=nativePackages_;
  for(const auto& package:packages){
    auto it=combined.find(package.id);
    if(it==combined.end()){if(combined.size()>=256)return false;combined.emplace(package.id,package);}
    else {
      if(_wcsicmp(it->second.rootPath.c_str(),package.rootPath.c_str())||_wcsicmp(it->second.packagePath.c_str(),package.packagePath.c_str()))return false;
      for(const auto& image:package.uiImages){if(std::any_of(it->second.uiImages.begin(),it->second.uiImages.end(),[&](const auto& prior){return prior.weapon==image.weapon&&prior.identity==image.identity;}))return false;it->second.uiImages.push_back(image);}
    }
  }
  auto vehicles=nativeDeclarations_;
  for(const auto& declaration:declarations){
    if(!combined.count(declaration.owner)||std::any_of(vehicles.begin(),vehicles.end(),[&](const auto& prior){return prior.identity==declaration.identity||_wcsicmp(prior.resourceName.c_str(),declaration.resourceName.c_str())==0;}))return false;
    vehicles.push_back(declaration);
  }
  nativePackages_=std::move(combined);nativeDeclarations_=std::move(vehicles);return true;
}
DWORD WINAPI Host::PrepareStartupCatalogThread(void* context) {
  if (!context) return ERROR_INVALID_PARAMETER;
  static_cast<Host*>(context)->PrepareStartupCatalog();
  return ERROR_SUCCESS;
}

void Host::PrepareStartupCatalog() {
  std::vector<host::ModDescriptor> nativePackages;
  std::vector<host::VehicleDeclaration> nativeDeclarations;
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);if(nativeDeclarationsFrozen_)return;
    nativeDeclarationsFrozen_=true;nativeDeclarations=nativeDeclarations_;
    for(const auto& entry:nativePackages_)nativePackages.push_back(entry.second);
  }
  const DWORD thread = GetCurrentThreadId();
  if (!api_.active_mod_count || !api_.active_mod_path) {
    InterlockedExchange(&startupCatalogState_, -1);
    Log("STARTUP_CATALOG_UNAVAILABLE", "host", "active mod snapshot API missing");
    return;
  }
  const int count = api_.active_mod_count();
  if (count < 0 || count > 256) {
    InterlockedExchange(&startupCatalogState_, -1);
    Log("STARTUP_CATALOG_UNAVAILABLE", "host", "activation snapshot incomplete");
    return;
  }
  std::vector<std::wstring> packages;
  packages.reserve(count);
  for (int i = 0; i < count; ++i) {
    wchar_t path[1024]{};
    if (!api_.active_mod_path(i, path, 1024)) {
      InterlockedExchange(&startupCatalogState_, -1);
      Log("STARTUP_CATALOG_UNAVAILABLE", "host", "invalid activation path");
      return;
    }
    packages.emplace_back(path);
  }
  auto discovered = host::DiscoverActiveBridgeMods(packages);
  auto declarations = host::CollectEarlyDeclarations(discovered);
  declarations.vehicles.insert(declarations.vehicles.end(),nativeDeclarations.begin(),nativeDeclarations.end());
  auto assetPackages=discovered;
  assetPackages.mods.insert(assetPackages.mods.end(),nativePackages.begin(),nativePackages.end());
  const bool hasUiResources = std::any_of(assetPackages.mods.begin(), assetPackages.mods.end(),
      [](const host::ModDescriptor& mod) { return !mod.uiImages.empty(); });
  if (hasUiResources) {
    wchar_t executable[32768]{};
    const DWORD executableLength = GetModuleFileNameW(nullptr, executable,
        static_cast<DWORD>(std::size(executable)));
    wchar_t localAppData[32768]{};
    const DWORD localAppDataLength = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData,
        static_cast<DWORD>(std::size(localAppData)));
    std::string error;
    if (!executableLength || executableLength >= std::size(executable) ||
        !localAppDataLength || localAppDataLength >= std::size(localAppData)) {
      discovered.diagnostics.push_back(L"UI_ATLAS_PREPARE_FAILED: process_or_cache_path_unavailable");
    } else {
      const auto executablePath = std::filesystem::path(executable);
      const auto vanillaAtlas = (executablePath.parent_path() / L"rom_0" / L"textures" /
          L"screen_ui" / L"index.atlas").wstring();
      const auto cacheRoot = (std::filesystem::path(localAppData) / L"Carrier Command 2" /
          L"CC2X" / L"ui-atlas-cache").wstring();
      api::ui::AtlasBuildResult atlas;
      if (!api::ui::PrepareAtlas(assetPackages, vanillaAtlas, cacheRoot, api_.game_build,
              &atlas, &error)) {
        discovered.diagnostics.push_back(L"UI_ATLAS_PREPARE_FAILED: " + Wide(error.c_str()));
        if (api_.log) api_.log("cc2x_ui_atlas_factory", CC2X_LOG_ERROR,
            "event=ATLAS_FACTORY_FAILED reason=%s", error.c_str());
      } else if (!atlas.resources.empty()) {
        std::string publishStatus;
        if (!api::ui::Registry().Publish(std::move(atlas.resources), &error)) {
          discovered.diagnostics.push_back(L"UI_RESOURCE_REGISTRY_FAILED: " + Wide(error.c_str()));
          publishStatus = error;
        } else if (!api::ui::second_atlas::Install(&api_)) {
          discovered.diagnostics.push_back(L"UI_ATLAS_HOOK_INSTALL_FAILED");
          publishStatus = "atlas_hook_install_failed";
        } else {
          publishStatus = "published";
        }
        if (api_.log) api_.log("cc2x_ui_atlas_factory", publishStatus == "published" ? CC2X_LOG_INFO : CC2X_LOG_WARN,
            "event=ATLAS_FACTORY_READY regions=%u cache=%s generated_small=%u registry=%s",
            static_cast<unsigned>(atlas.regions), atlas.cacheHit ? "hit" : "miss",
            static_cast<unsigned>(atlas.generatedSmallIcons), publishStatus.c_str());
      }
    }
  }
  const std::uint32_t firstId=102u;
  std::string registryError;
  if (!api::vehicle::Registry().Publish(std::move(declarations.vehicles),firstId,&registryError))
    declarations.diagnostics.push_back(L"DEFINITION_PLAN_FAILED: "+Wide(registryError.c_str()));
  const auto mods = discovered.mods.size();
  const auto diagnostics = discovered.diagnostics.size();
  const auto vehicles = api::vehicle::Registry().Count();
  const auto declarationDiagnostics = declarations.diagnostics.size();
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    startupCatalog_ = std::move(discovered);
    earlyCatalog_ = std::move(declarations);
    InterlockedExchange(&startupCatalogState_, 1);
  }
  char detail[200]{};
  sprintf_s(detail, "thread=%lu active_packages=%d bridge_mods=%zu diagnostics=%zu early_vehicles=%zu early_diagnostics=%zu",
      static_cast<unsigned long>(thread), count, mods, diagnostics, vehicles, declarationDiagnostics);
  Log("STARTUP_CATALOG_READY", "host", detail);
}

std::string Host::ResolveEarlyVehicle(const std::string& owner,const std::string& localId) const {
  const auto identity=Wide((owner+"."+localId).c_str());
  const auto* vehicle=api::vehicle::Registry().Find(identity);
  return vehicle && Utf8(vehicle->descriptor.owner)==owner ? Utf8(vehicle->descriptor.identity) : std::string{};
}

void Host::ObserveGameResourceInit() {
  InterlockedExchange(&resourceInitCatalogState_,
      InterlockedCompareExchange(&startupCatalogState_, 0, 0));
  InterlockedExchange(&resourceInitThread_, static_cast<LONG>(GetCurrentThreadId()));
  InterlockedExchange(&resourceInitObserved_, 1);
}

void Host::Load() {
  const LONG catalogState=InterlockedCompareExchange(&startupCatalogState_,0,0);
  if(catalogState==0)return;
  loaded_=true;
  if(catalogState<0){Log("STARTUP_CATALOG_UNAVAILABLE","host");return;}
  std::error_code ec;std::filesystem::create_directories(storage_,ec);
  const auto sdk=storage_+L"\\cc2.lua";
  {std::ofstream file(std::filesystem::path(sdk),std::ios::binary|std::ios::trunc);file<<kBridgeSdk;if(!file){Log("SDK_WRITE_ERROR","host");return;}}
  // Native definitions are process-lifetime catalog data. A hot reload may
  // rediscover mods, but its vehicle declarations must match this frozen catalog.
  host::ModDiscovery discovered;
  const bool initialLoad=!runtimeInitialLoadDone_;
  if(initialLoad) discovered=startupCatalog_;
  else {
    const int count=api_.active_mod_count();
    if(count<0||count>256){Log("ACTIVATION_UNAVAILABLE","host");return;}
    std::vector<std::wstring> packages;
    for(int i=0;i<count;++i){wchar_t path[1024]{};if(api_.active_mod_path(i,path,1024))packages.emplace_back(path);else Log("ACTIVATION_PATH_ERROR","host");}
    discovered=host::DiscoverActiveBridgeMods(packages);
    auto declarations=host::CollectEarlyDeclarations(discovered);
    declarations.vehicles.insert(declarations.vehicles.end(),nativeDeclarations_.begin(),nativeDeclarations_.end());
    std::string catalogError;
    if(!api::vehicle::Registry().MatchesCatalog(std::move(declarations.vehicles),&catalogError)) {
      const auto detail="restart_required=1; "+catalogError;
      Log("NATIVE_CATALOG_RELOAD_REJECTED","host",detail.c_str());
      runtimeInitialLoadDone_=true;
      return;
    }
  }
  runtimeInitialLoadDone_=true;
  for(const auto& d:discovered.diagnostics)Log("DISCOVERY","host",Utf8(d).c_str());
  if(initialLoad) for(const auto& d:earlyCatalog_.diagnostics)
    Log("EARLY_DECLARATION","host",Utf8(d).c_str());
  for(const auto& mod:discovered.mods) {
    std::wstring bootstrap,error;
    if(!host::WriteBridgeBootstrap(mod,sdk,storage_,&bootstrap,&error)){Log("BOOTSTRAP_ERROR",Utf8(mod.id).c_str(),Utf8(error).c_str());continue;}
    auto m=std::make_unique<Module>();m->host=this;m->id=Utf8(mod.id);m->generation=++metrics_.vmGeneration;
    m->descriptor=mod;m->sdkPath=sdk;
    m->L=lua_newstate(Allocate,m.get());if(!m->L){Log("VM_ALLOCATION_ERROR",m->id.c_str());continue;}
    if(!mailbox_.Enable(m->id)){lua_close(m->L);Log("MODULE_ENDPOINT_ERROR",m->id.c_str());continue;}
    *static_cast<Module**>(lua_getextraspace(m->L))=m.get();SetBudget(*m);
    OpenLibraries(m->L);Register(*m);
    auto path=Utf8(bootstrap);
    int rc=ReadLua(m->L,path.c_str());if(rc==LUA_OK)rc=lua_pcall(m->L,0,0,0);
    if(rc!=LUA_OK){Log("MODULE_LOAD_ERROR",m->id.c_str(),lua_tostring(m->L,-1));mailbox_.Remove(m->id);cc2x_entity_type_remove_owner(m->id.c_str());lua_close(m->L);metrics_.dropped+=commands_.size();auto epoch=commands_.active;commands_.End(epoch);commands_.Begin(epoch);continue;}
    ++metrics_.loads;Log("MODULE_LOADED",m->id.c_str());modules_.push_back(std::move(m));
  }
}
void Host::CloseModules() {
  for(auto& m:modules_){Call(*m,"__cc2_host_unload",0,false);DropTasks("lua."+m->id);mailbox_.Remove(m->id);cc2x_entity_type_remove_owner(m->id.c_str());lua_close(m->L);m->L=nullptr;++metrics_.unloads;Log("VM_CLOSED",m->id.c_str());}
  modules_.clear();loaded_=false;metrics_.dropped+=commands_.size();auto epoch=commands_.active;commands_.End(epoch);if(epoch)commands_.Begin(epoch);
  for(auto& m:ruleModules_){Call(*m,"__cc2_host_unload",0,false);lua_close(m->L);m->L=nullptr;}
  ruleModules_.clear();rulesLoaded_=false;
  // Animation coefficients are process-wide and shared with stock Lua. Module
  // teardown cannot infer ownership; explicit x1 or process exit restores stock.
}
void Host::Tick(void* server) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(!attached_||stopped_||!server_||server_!=server)return;
  if(inLua_){++metrics_.wrongThread;Log("REENTRANT_TICK_REJECTED","host");return;}
  DWORD current=GetCurrentThreadId();
  if(threadGeneration_==metrics_.generation&&thread_!=current){++metrics_.wrongThread;Log("WRONG_THREAD","host");return;}
  if(thread_!=current)Log("VM_THREAD_MIGRATION","host","serialized; new session generation");
  thread_=current;threadGeneration_=metrics_.generation;inLua_=true;
  const auto taskCutoff=nextNativeId_; // posts made inside this tick run on a later tick
  if(!resourceInitLogged_) {
    resourceInitLogged_=true;
    char detail[160]{};
    sprintf_s(detail,"observed=%ld catalog_state_at_entry=%ld game_thread=%ld runtime_thread=%lu",
        InterlockedCompareExchange(&resourceInitObserved_,0,0),
        InterlockedCompareExchange(&resourceInitCatalogState_,0,0),
        InterlockedCompareExchange(&resourceInitThread_,0,0),
        static_cast<unsigned long>(current));
    Log("EARLY_RESOURCE_INIT_ORDER","host",detail);
  }
  cc2::api::vehicle::definition_adapter::Flush(&api_);
  ULONGLONG now=GetTickCount64();double dt=lastTick_?std::min(0.25,(now-lastTick_)/1000.0):1.0/30.0;lastTick_=now;
  if(reload_){CloseModules();reload_=false;}
  if(!loaded_)Load();
  ++metrics_.updates;++sessionTicks_;
  RunTasks(taskCutoff);
  for(auto& m:modules_){Call(*m,"__cc2_host_update",dt,true);++m->updates;}
  const bool report=now-lastReport_>=5000;
  if(report){lastReport_=now;for(auto& m:modules_){char detail[200];sprintf_s(detail,"vm=%llu updates=%llu callbacks=%llu errors=%llu bytes=%zu",m->generation,m->updates,m->callbacks,m->errors,m->allocated);Log("MODULE_COUNTERS",m->id.c_str(),detail);for(auto&entry:m->handlers){sprintf_s(detail,"vm=%llu handler=%u calls=%llu errors=%llu",m->generation,entry.first,entry.second.calls,entry.second.errors);Log("HANDLER_COUNTERS",m->id.c_str(),detail);}}}
  if(report){char detail[240];sprintf_s(detail,"updates=%llu modules=%zu requested=%llu applied=%llu dropped=%llu queued=%zu wrong_thread=%llu",metrics_.updates,modules_.size(),metrics_.requested,metrics_.applied,metrics_.dropped,commands_.size(),metrics_.wrongThread);Log("HOST_COUNTERS","host",detail);}
  if(report&&worldReads_.calls){char detail[240];sprintf_s(detail,"calls=%llu failures=%llu wrong_thread=%llu objects=%llu total_us=%llu max_us=%llu",worldReads_.calls,worldReads_.failures,worldReads_.wrongThread,worldReads_.objects,worldReads_.microseconds,worldReads_.maxMicroseconds);Log("WORLD_API_STATS","host",detail);worldReads_={};}
  if(hangarSpawnRequested_||hangarSpawnRollbackPending_) {
    const auto requestGeneration=hangarSpawnRequestGeneration_;
    const bool requested=hangarSpawnRequested_;
    hangarSpawnRequested_=false;
    inLua_=false;
    if(requested&&(requestGeneration!=metrics_.generation||server_!=server)) {
      hangarSpawnResultSuccess_=false;hangarSpawnResultVehicleId_=0;
      hangarSpawnResultError_=CC2X_HANGAR_SPAWN_SESSION_CHANGED;
      ++metrics_.dropped;LogHangarSpawn("HANGAR_SPAWN_DROPPED","session generation changed before execution");
      CompleteHangarSpawn();
    } else if(requested) {
      hangarSpawnInFlight_=true;hangarSpawnAttemptGeneration_=metrics_.generation;
      hangarSpawnResultSuccess_=false;hangarSpawnResultVehicleId_=0;
      hangarSpawnResultError_=CC2X_HANGAR_SPAWN_NATIVE_OPERATION_FAILED;
      hangarSpawnResultSent_=false;
      try { ProcessHangarSpawn(server); }
      catch(...) {
        hangarSpawnUnsafeLatched_=true;
        LogHangarSpawn("HANGAR_SPAWN_UNHANDLED_EXCEPTION","outcome unknown; further requests blocked for this session");
      }
      CompleteHangarSpawn();
      hangarSpawnInFlight_=false;
    } else ProcessHangarSpawn(server);
  }
  inLua_=false;
}
void Host::LogHangarSpawn(const char* event,const char* detail) {
  if(event&&std::strcmp(event,"HANGAR_SPAWN_PREFLIGHT_FAIL")==0) {
    const char* why=detail?detail:"";
    if(std::strstr(why,"carrier_id_not_found"))hangarSpawnResultError_=CC2X_HANGAR_SPAWN_INVALID_CARRIER;
    else if(std::strstr(why,"slot_out_of_range")||std::strstr(why,"slot_record"))hangarSpawnResultError_=CC2X_HANGAR_SPAWN_INVALID_SLOT;
    else if(std::strstr(why,"slot_occupied"))hangarSpawnResultError_=CC2X_HANGAR_SPAWN_SLOT_OCCUPIED;
    else if(std::strstr(why,"compatible_with_chassis"))hangarSpawnResultError_=CC2X_HANGAR_SPAWN_UNSUPPORTED_VEHICLE_TYPE;
    else if(std::strstr(why,"unsupported_game_build")||std::strstr(why,"sha256_mismatch"))hangarSpawnResultError_=CC2X_HANGAR_SPAWN_UNSUPPORTED_GAME_BUILD;
    else if(std::strstr(why,"server_roots")||std::strstr(why,"carrier_layout")||std::strstr(why,"owner_or_definition"))hangarSpawnResultError_=CC2X_HANGAR_SPAWN_WORLD_NOT_READY;
  } else if(event&&std::strcmp(event,"HANGAR_SPAWN_DOCKED_AND_PUBLISHED")==0) {
    hangarSpawnResultSuccess_=true;hangarSpawnResultError_=0;
  }
  char tagged[512]{};
  sprintf_s(tagged,"request=%u %s",hangarSpawnRequestId_,detail?detail:"");
  Log(event,"carrier.hangar",tagged);
}
void Host::CompleteHangarSpawn() {
  if(!hangarSpawnRequestId_||hangarSpawnResultSent_)return;
  cc2x_native_hangar_spawn_result(hangarSpawnRequestId_,hangarSpawnResultSuccess_?1:0,
      hangarSpawnResultVehicleId_,hangarSpawnResultSuccess_?0:hangarSpawnResultError_);
  hangarSpawnResultSent_=true;
}
void Host::RecordHangarSpawnResult(std::uint32_t requestId,int success,std::uint32_t vehicleId,std::uint32_t errorCode) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  HangarSpawnResult result{requestId,success!=0,vehicleId,errorCode};
  hangarSpawnResults_.push_back(result);
  if(hangarSpawnResults_.size()>128)hangarSpawnResults_.erase(hangarSpawnResults_.begin());
  char detail[160];sprintf_s(detail,"request=%u success=%d vehicle_id=%u error_code=%u",requestId,success,vehicleId,errorCode);
  Log("HANGAR_SPAWN_RESULT","carrier.hangar",detail);
}
bool Host::FindHangarSpawnResult(std::uint32_t requestId,HangarSpawnResult* result) {
  if(!result)return false;
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  for(auto it=hangarSpawnResults_.rbegin();it!=hangarSpawnResults_.rend();++it)if(it->requestId==requestId){*result=*it;return true;}
  return false;
}
std::uint32_t Host::QueueHangarSpawn(std::uint32_t carrierId,std::uint32_t slot,std::uint32_t chassisType) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(!attached_||stopped_||!nativeExtensionsReady_||!api_.carrier_hangar_spawn||!server_||!carrierId)return 0;
  if(hangarSpawnRequested_||hangarSpawnInFlight_||hangarSpawnUnsafeLatched_||hangarSpawnRollbackPending_) {
    LogHangarSpawn("HANGAR_SPAWN_REJECTED","request already queued, operation active, unresolved rollback, or unsafe outcome latched");return 0;
  }
  hangarSpawnCarrierId_=carrierId;hangarSpawnSlot_=slot;hangarSpawnType_=chassisType;
  hangarSpawnRequested_=true;hangarSpawnRequestGeneration_=metrics_.generation;
  if(hangarSpawnRequestId_>=static_cast<std::uint32_t>(INT_MAX)) {
    hangarSpawnRequestId_=0;
    hangarSpawnResults_.clear(); // Never alias a previous result after wrap.
  }
  ++hangarSpawnRequestId_;
  hangarSpawnResultSent_=false;
  char detail[192]{};
  sprintf_s(detail,"Lua requested carrier_id=%u slot=%u chassis_type=%u; deferred to server tick",
      carrierId,slot,chassisType);
  LogHangarSpawn("HANGAR_SPAWN_REQUESTED",detail);
  return hangarSpawnRequestId_;
}
void Host::RequestReload(){std::lock_guard<std::recursive_mutex>lock(mutex_);reload_=true;}
void Host::Shutdown(){std::lock_guard<std::recursive_mutex>lock(mutex_);if(stopped_)return;stopped_=true;inLua_=false;CloseModules();nativeListeners_.clear();nativeBindings_.clear();for(const auto& owner:nativeEntityOwners_)cc2x_entity_type_remove_owner(owner.c_str());nativeEntityOwners_.clear();tasks_.clear();for(const auto& endpoint:nativeMailboxes_)mailbox_.Remove(endpoint.second);nativeMailboxes_.clear();mailbox_.End();commands_.Enable(false);server_=nullptr;if(g_stockHost==this){g_stockHost=nullptr;g_stockLuaApi=nullptr;}}
Host* SharedHost(){return g_stockHost;}
Metrics Host::Stats(){std::lock_guard<std::recursive_mutex>lock(mutex_);auto out=metrics_;out.modules=modules_.size();out.callbacks=out.errors=0;for(auto&m:modules_){out.callbacks+=m->callbacks;out.errors+=m->errors;}return out;}
}
