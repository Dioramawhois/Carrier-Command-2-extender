// SPDX-License-Identifier: GPL-3.0-or-later
#include "cc2x_framework.h"
#include "runtime/host.h"
#include "api/vehicle/control.h"
#include "api/vehicle/definition_registry.h"
#include "platform/seh_guard.h"
#include "entity_registry.h"
extern "C" {
#include "lua.h"
}
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {
using cc2::runtime::Host;
Host* Owner(){return cc2::runtime::SharedHost();}
struct PluginTransaction {
  bool active=false;
  std::wstring directory;
  std::map<std::string,cc2::host::ModDescriptor> packages;
  std::vector<cc2::host::VehicleDeclaration> vehicles;
  struct Registration {int kind;std::uint64_t id;};
  std::vector<Registration> registrations;
};
thread_local PluginTransaction pluginTransaction;
std::uint64_t Track(int kind,std::uint64_t id){if(id&&pluginTransaction.active)pluginTransaction.registrations.push_back({kind,id});return id;}
bool Text(const char* value,std::string* out,std::size_t maximum=128) {
  if(!value)return false;const auto n=strnlen(value,maximum+1);
  if(!n||n>maximum)return false;out->assign(value,n);return true;
}
std::uint64_t Capabilities() {
  auto* h=Owner();if(!h)return 0;std::uint64_t flags=CC2X_FRAMEWORK_MESSAGES|CC2X_FRAMEWORK_LUA_BINDINGS;
  const auto* a=h->Api();if(a->find&&a->geti&&a->getf&&a->seti&&a->setf)flags|=CC2X_FRAMEWORK_WORLD;
  if(cc2::api::vehicle::ButtonsSupported())flags|=CC2X_FRAMEWORK_BUTTONS;
  if(a->game_build&&(std::strcmp(a->game_build,"1.5.18-win32-r2")==0||std::strcmp(a->game_build,"1.5.18-win32")==0))flags|=CC2X_FRAMEWORK_THROTTLE;
  return flags|CC2X_FRAMEWORK_VEHICLE_CATALOG|CC2X_FRAMEWORK_ENTITY_METADATA|CC2X_FRAMEWORK_SESSION_TASKS;
}
std::uint64_t Subscribe(const char* event,cc2x_framework_event_fn callback,void* user) {
  auto* h=Owner();std::string name;return h&&Text(event,&name,64)?Track(0,h->SubscribeNative(name,callback,user)):0;
}
int Unsubscribe(std::uint64_t id){auto* h=Owner();return h&&h->UnsubscribeNative(id);}
int Key(std::uint32_t key){auto* h=Owner();return h&&key>0&&key<=255&&h->KeyDown(static_cast<int>(key));}
int ThrottleGet(std::uint32_t id,double* value){auto* h=Owner();return h&&h->ReadVehicleThrottle(id,value);}
int ThrottleSet(std::uint32_t id,double value){auto* h=Owner();return h&&h->WriteVehicleThrottle(id,value);}
int ButtonCount(std::uint32_t id) {
  auto* h=Owner();auto* c=h?h->CurrentButtons():nullptr;return c&&c->vehicleId==id?static_cast<int>(c->buttons.size()):-1;
}
int ButtonGet(std::uint32_t id,std::uint32_t index,cc2x_button_info* out) {
  auto* h=Owner();auto* c=h?h->CurrentButtons():nullptr;
  if(!c||c->vehicleId!=id||!out||index>=c->buttons.size())return 0;
  const auto& b=c->buttons[index];if(b.name.size()>=sizeof(out->name))return 0;
  std::memset(out,0,sizeof(*out));out->index=b.index;out->on=b.on;out->enabled=b.enabled;std::memcpy(out->name,b.name.data(),b.name.size());return 1;
}
int ButtonsSet(std::uint32_t id,const cc2x_button_value* values,std::uint32_t count) {
  auto* h=Owner();auto* c=h?h->CurrentButtons():nullptr;if(!c||c->vehicleId!=id||!values||!count||count>64)return 0;
  std::map<std::string,bool> requested;
  for(std::uint32_t i=0;i<count;++i){std::string name;
    if(!Text(values[i].name,&name,127)||values[i].on>1||!requested.emplace(name,values[i].on!=0).second)return 0;
  }
  return cc2::api::vehicle::StageButtons(c,requested);
}
std::uint64_t MailboxOpen(const char* owner){auto* h=Owner();std::string name;return h&&Text(owner,&name)?Track(2,h->OpenNativeMailbox(name)):0;}
int MailboxClose(std::uint64_t id){auto* h=Owner();return h&&h->CloseNativeMailbox(id);}
int MailboxPoll(std::uint64_t id,cc2x_message* out,std::uint32_t capacity){auto* h=Owner();return h?h->PollNativeMailbox(id,out,capacity):-1;}
int MailboxPublish(std::uint64_t id,const char* topic,std::uint32_t subject,double value){auto* h=Owner();std::string name;return h&&Text(topic,&name,64)&&h->PublishNativeMailbox(id,name,subject,value);}
int MailboxForget(std::uint64_t id,std::uint32_t subject){auto* h=Owner();return h&&h->ForgetNativeMailbox(id,subject);}
int MessageSend(const char* owner,const char* topic,std::uint32_t subject,double value){auto* h=Owner();std::string target,name;return h&&Text(owner,&target)&&Text(topic,&name,64)&&h->Mailbox().Send(target,name,subject,value);}
int MessageRead(const char* owner,const char* topic,std::uint32_t subject,double* out){auto* h=Owner();std::string target,name;return h&&Text(owner,&target)&&Text(topic,&name,64)&&h->Mailbox().Read(target,name,subject,out);}
std::uint64_t Bind(const char* owner,const char* name,cc2x_vm_fn callback,void* user){auto* h=Owner();std::string space,function;return h&&Text(owner,&space,64)&&Text(name,&function,64)?Track(1,h->BindNative(space,function,callback,user)):0;}
int Unbind(std::uint64_t id){auto* h=Owner();return h&&h->UnbindNative(id);}

thread_local lua_State* currentVm=nullptr;
lua_State* Vm(cc2x_vm* vm){auto* state=reinterpret_cast<lua_State*>(vm);return state&&state==currentVm?state:nullptr;}
int VmTop(cc2x_vm* vm){auto* L=Vm(vm);return L?lua_gettop(L):-1;}
int VmType(cc2x_vm* vm,int index){auto* L=Vm(vm);return L?lua_type(L,index):CC2X_LUA_NONE;}
int VmInteger(cc2x_vm* vm,int index,std::int64_t* out){auto* L=Vm(vm);if(!L||!out)return 0;int ok=0;auto value=lua_tointegerx(L,index,&ok);if(ok)*out=value;return ok;}
int VmNumber(cc2x_vm* vm,int index,double* out){auto* L=Vm(vm);if(!L||!out)return 0;int ok=0;auto value=lua_tonumberx(L,index,&ok);if(ok)*out=value;return ok;}
int VmBoolean(cc2x_vm* vm,int index){auto* L=Vm(vm);return L&&lua_toboolean(L,index);}
const char* VmString(cc2x_vm* vm,int index,std::size_t* length){auto* L=Vm(vm);if(!L||lua_type(L,index)!=LUA_TSTRING){if(length)*length=0;return nullptr;}return lua_tolstring(L,index,length);}
void VmNil(cc2x_vm* vm){if(auto* L=Vm(vm))lua_pushnil(L);}
void VmPushBoolean(cc2x_vm* vm,int value){if(auto* L=Vm(vm))lua_pushboolean(L,value);}
void VmPushInteger(cc2x_vm* vm,std::int64_t value){if(auto* L=Vm(vm))lua_pushinteger(L,value);}
void VmPushNumber(cc2x_vm* vm,double value){if(auto* L=Vm(vm))lua_pushnumber(L,value);}
void VmPushString(cc2x_vm* vm,const char* value){if(auto* L=Vm(vm)){if(!value)lua_pushnil(L);else if(strnlen(value,1024*1024+1)<=1024*1024)lua_pushstring(L,value);}}
void VmTable(cc2x_vm* vm){if(auto* L=Vm(vm))lua_newtable(L);}
void VmField(cc2x_vm* vm,const char* key){auto* L=Vm(vm);std::string name;if(L&&lua_gettop(L)>=2&&lua_istable(L,-2)&&Text(key,&name))lua_setfield(L,-2,name.c_str());}
void VmAppend(cc2x_vm* vm,std::int64_t index){auto* L=Vm(vm);if(L&&lua_gettop(L)>=2&&lua_istable(L,-2)&&index>=1)lua_rawseti(L,-2,index);}
const cc2x_vm_api stackApi{sizeof(cc2x_vm_api),VmTop,VmType,VmInteger,VmNumber,VmBoolean,VmString,
  VmNil,VmPushBoolean,VmPushInteger,VmPushNumber,VmPushString,VmTable,VmField,VmAppend};
struct BindingCall {cc2x_vm_fn callback;cc2x_vm* vm;void* user;int result=0;};
void RunBinding(void* opaque){auto* call=static_cast<BindingCall*>(opaque);call->result=call->callback(call->vm,call->user);}
std::uint64_t SessionGeneration(){auto* h=Owner();return h?h->ActiveGeneration():0;}
std::uint64_t SessionTick(){auto* h=Owner();return h?h->ActiveTick():0;}
struct TaskCall {cc2x_framework_event_fn callback;const cc2x_framework_event* event;void* user;};
void RunTask(void* opaque){auto* call=static_cast<TaskCall*>(opaque);call->callback(call->event,call->user);}
std::uint64_t Post(const char* owner,std::uint64_t generation,cc2x_framework_event_fn callback,void* user) {
  auto* h=Owner();std::string name;if(!h||!callback||!Text(owner,&name,96)||name.rfind("native.",0)!=0)return 0;
  return h->PostTask(name,generation,[h,callback,user]{
    const cc2x_framework_event event{sizeof(event),"task",h->ActiveGeneration(),h->ActiveTick(),0,0,0};
    TaskCall call{callback,&event,user};if(!cc2x_seh_guard(RunTask,&call))h->Log("NATIVE_TASK_ERROR","framework");
  });
}
int Cancel(const char* owner,std::uint64_t id){auto* h=Owner();std::string name;
  return h&&Text(owner,&name,96)&&name.rfind("native.",0)==0&&h->CancelTask(name,id);}

std::uint32_t DefinitionCount(){return static_cast<std::uint32_t>(cc2::api::vehicle::Registry().Count());}
int DefinitionGet(std::uint32_t index,cc2x_vehicle_definition* out) {
  auto& registry=cc2::api::vehicle::Registry();if(!out||index>=registry.Count())return 0;
  std::uint32_t seen=0;
  for(auto id=registry.FirstId();id<registry.Limit();++id){const auto* record=registry.Find(id);if(!record)continue;
    if(seen++!=index)continue;
    const auto& identity=record->descriptor.identity;int count=WideCharToMultiByte(CP_UTF8,0,identity.c_str(),-1,nullptr,0,nullptr,nullptr);
    if(count<=0||static_cast<std::size_t>(count)>sizeof(out->identity)||record->displayName.size()>=sizeof(out->name))return 0;
    std::memset(out,0,sizeof(*out));out->definition_id=id;out->base_type=record->descriptor.nativeBaseType;out->flags=record->state.load(std::memory_order_acquire);
    WideCharToMultiByte(CP_UTF8,0,identity.c_str(),-1,out->identity,sizeof(out->identity),nullptr,nullptr);
    std::memcpy(out->name,record->displayName.data(),record->displayName.size());return 1;
  }return 0;
}
int VehicleSpawn(const char* identity,std::uint32_t carrier,std::uint32_t slot) {
  auto* h=Owner();std::string value;if(!h||!Text(identity,&value,129)||!carrier)return 0;
  int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);if(count<=0)return 0;
  std::wstring wide(static_cast<std::size_t>(count),L'\0');if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),wide.data(),count))return 0;
  auto& registry=cc2::api::vehicle::Registry();const auto* record=registry.Find(wide);
  return record&&registry.ReadyFor(record->descriptor.definitionId)?static_cast<int>(h->QueueHangarSpawn(carrier,slot,record->descriptor.definitionId)):0;
}
bool EntityOwner(Host* host,const char* owner,std::string* name) {
  return host&&host->InServerContext()&&Text(owner,name,96)&&name->rfind("native.",0)==0;
}
int EntityRegister(const char* owner,const char* id,const char* base,const char* definition) {
  auto* h=Owner();std::string name;
  return EntityOwner(h,owner,&name)&&h->RegisterNativeEntity(name,id,base,definition);
}
int EntityRemove(const char* owner,const char* id) {
  auto* h=Owner();std::string name;
  return EntityOwner(h,owner,&name)&&cc2x_entity_type_remove(name.c_str(),id);
}
int EntityClear(const char* owner) {
  auto* h=Owner();std::string name;
  return EntityOwner(h,owner,&name)?cc2x_entity_type_remove_owner(name.c_str()):-1;
}
int EntityCount(){auto* h=Owner();return h&&h->InServerContext()?cc2x_entity_type_count():-1;}
int EntityGet(std::uint32_t index,cc2x_entity_metadata* out) {
  auto* h=Owner();if(!h||!h->InServerContext()||!out||index>=CC2X_ENTITY_TYPE_CAP)return 0;
  cc2x_entity_type record{};if(!cc2x_entity_type_get(static_cast<int>(index),&record))return 0;
  static_assert(sizeof(record)==sizeof(*out));std::memcpy(out,&record,sizeof(record));return 1;
}
int HangarSlots(std::uint32_t carrier,std::uint32_t type,cc2x_hangar_slot* out,std::uint32_t capacity) {
  auto* h=Owner();if(!h||!h->InServerContext()||(!out&&capacity))return -1;
  std::vector<Host::HangarSlotOption> slots;std::string reason;
  if(!h->DescribeHangarSlots(carrier,type,&slots,&reason))return -1;
  if(out&&capacity>=slots.size())for(std::size_t i=0;i<slots.size();++i)out[i]={slots[i].index,slots[i].compatible,slots[i].occupied};
  return static_cast<int>(slots.size());
}
int HangarResult(std::uint32_t request,cc2x_hangar_result* out) {
  auto* h=Owner();if(!h||!h->InServerContext()||!out)return 0;
  Host::HangarSpawnResult result{};if(!h->FindHangarSpawnResult(request,&result))return 0;
  *out={result.requestId,result.success,result.vehicleId,result.errorCode};return 1;
}
cc2::host::ModDescriptor* NativePackage(const char* owner) {
  auto* h=Owner();std::string name;if(!h||!pluginTransaction.active||!Text(owner,&name,64))return nullptr;
  auto it=pluginTransaction.packages.find(name);if(it!=pluginTransaction.packages.end())return &it->second;
  cc2::host::ModDescriptor package;if(!h->DescribeNativePackage(name,pluginTransaction.directory,&package))return nullptr;
  return &pluginTransaction.packages.emplace(name,std::move(package)).first->second;
}
int DeclareVehicle(const char* owner,const cc2x_vehicle_declaration* declaration) {
  if(!declaration||pluginTransaction.vehicles.size()>=256)return 0;
  auto* package=NativePackage(owner);if(!package)return 0;
  cc2::host::VehicleDeclaration vehicle;std::string error;
  if(!cc2::host::BuildVehicleDeclarationFromC(*package,*declaration,&vehicle,&error)) {
    Owner()->Log("NATIVE_DECLARATION_REJECTED",owner,error.c_str());return 0;
  }
  if(std::any_of(pluginTransaction.vehicles.begin(),pluginTransaction.vehicles.end(),[&](const auto& prior){return prior.identity==vehicle.identity||_wcsicmp(prior.resourceName.c_str(),vehicle.resourceName.c_str())==0;}))return 0;
  pluginTransaction.vehicles.push_back(std::move(vehicle));return 1;
}
bool Utf8Field(const char* text,std::wstring* value) {
  if(!text){value->clear();return true;}const auto length=strnlen(text,1025);if(length>1024)return false;
  if(!length){value->clear();return true;}
  int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,static_cast<int>(length),nullptr,0);if(n<=0)return false;
  value->assign(static_cast<std::size_t>(n),L'\0');return MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,static_cast<int>(length),value->data(),n)==n;
}
int DeclareUi(const char* owner,const cc2x_ui_declaration* declaration) {
  if(!declaration||declaration->size!=sizeof(*declaration)||declaration->weapon>1)return 0;
  auto* package=NativePackage(owner);if(!package||package->uiImages.size()>=256)return 0;
  cc2::host::UiImageDescriptor image;image.weapon=declaration->weapon!=0;
  if(!Utf8Field(declaration->id,&image.identity)||!Utf8Field(declaration->small,&image.small)||
    !Utf8Field(declaration->large,&image.large)||!Utf8Field(declaration->loadout,&image.loadout)||!Utf8Field(declaration->engine_type,&image.engineType))return 0;
  std::transform(image.engineType.begin(),image.engineType.end(),image.engineType.begin(),towlower);
  std::wstring error;if(!cc2::host::ValidateUiImageDeclaration(*package,image,&error))return 0;
  if(std::any_of(package->uiImages.begin(),package->uiImages.end(),[&](const auto& prior){return prior.weapon==image.weapon&&prior.identity==image.identity;}))return 0;
  package->uiImages.push_back(std::move(image));return 1;
}
const cc2x_framework_api framework=[] {
  cc2x_framework_api api{};api.version=CC2X_FRAMEWORK_VERSION;api.size=sizeof(api);api.interface_id=CC2X_FRAMEWORK_ID;
  api.capabilities=Capabilities;api.subscribe=Subscribe;api.unsubscribe=Unsubscribe;api.key_down=Key;
  api.throttle_get=ThrottleGet;api.throttle_set=ThrottleSet;api.button_count=ButtonCount;api.button_get=ButtonGet;api.buttons_set=ButtonsSet;
  api.mailbox_open=MailboxOpen;api.mailbox_close=MailboxClose;api.mailbox_poll=MailboxPoll;api.mailbox_publish=MailboxPublish;api.mailbox_forget=MailboxForget;
  api.message_send=MessageSend;api.message_read=MessageRead;api.vm_bind=Bind;api.vm_unbind=Unbind;api.vm=&stackApi;
  api.declare_vehicle=DeclareVehicle;api.declare_ui=DeclareUi;
  api.vehicle_definition_count=DefinitionCount;api.vehicle_definition_get=DefinitionGet;api.vehicle_spawn=VehicleSpawn;
  api.entity_register=EntityRegister;api.entity_remove=EntityRemove;api.entity_clear=EntityClear;
  api.entity_count=EntityCount;api.entity_get=EntityGet;
  api.hangar_slots=HangarSlots;api.hangar_result=HangarResult;
  api.session_generation=SessionGeneration;api.session_tick=SessionTick;api.post=Post;api.cancel=Cancel;
  return api;
}();
}

namespace cc2::runtime {
int InvokeNativeBinding(lua_State* L,Host* host,std::uint64_t binding) {
  cc2x_vm_fn callback=nullptr;void* user=nullptr;
  if(!host||!host->FindNativeBinding(binding,&callback,&user)){lua_pushnil(L);lua_pushliteral(L,"native_binding_unavailable");return 2;}
  auto* previous=currentVm;currentVm=L;BindingCall call{callback,reinterpret_cast<cc2x_vm*>(L),user};
  const bool ok=cc2x_seh_guard(RunBinding,&call)!=0;currentVm=previous;
  if(!ok||call.result<0||call.result>64||call.result>lua_gettop(L)){lua_settop(L,0);lua_pushnil(L);lua_pushliteral(L,"native_binding_failed");return 2;}
  return call.result;
}
}

extern "C" CC2X_EXPORT const cc2x_framework_api* cc2x_query_framework_v1(std::uint32_t version,std::uint32_t size) {
  if(version!=CC2X_FRAMEWORK_VERSION||size!=sizeof(cc2x_framework_api)||!Owner())return nullptr;
  return &framework;
}
// Called only around the loader's native plugin_load boundary. Keeping the
// UTF-16 directory here avoids the legacy ANSI plugin_dir path limitation.
extern "C" void cc2x_native_bridge_plugin_begin(const wchar_t* directory) {
  pluginTransaction={};pluginTransaction.active=Owner()&&directory;pluginTransaction.directory=directory?directory:L"";
}
extern "C" int cc2x_native_bridge_plugin_end(int accepted) {
  if(!pluginTransaction.active)return accepted;
  auto* h=Owner();bool ok=accepted!=0&&h;
  if(ok&&(!pluginTransaction.packages.empty()||!pluginTransaction.vehicles.empty())) {
    std::vector<cc2::host::ModDescriptor> packages;for(const auto& entry:pluginTransaction.packages)packages.push_back(entry.second);
    ok=h->CommitNativeDeclarations(packages,pluginTransaction.vehicles);
  }
  if(!ok&&h)for(auto it=pluginTransaction.registrations.rbegin();it!=pluginTransaction.registrations.rend();++it) {
    if(it->kind==0)h->UnsubscribeNative(it->id);else if(it->kind==1)h->UnbindNative(it->id);else h->CloseNativeMailbox(it->id);
  }
  pluginTransaction={};return ok?1:0;
}
