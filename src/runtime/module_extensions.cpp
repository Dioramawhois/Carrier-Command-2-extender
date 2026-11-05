// SPDX-License-Identifier: GPL-3.0-or-later
#include "module_extensions.h"
#include "host.h"
#include "cc2x_carrier.h"
#include "cc2x_settings.h"
#include "cc2x_equipment.h"
#include "cc2x_tile_sites.h"
extern "C" {
#include "lua.h"
}
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
namespace cc2::runtime {
namespace {
Host* H(lua_State* L){return static_cast<Host*>(lua_touserdata(L,lua_upvalueindex(1)));}
std::string O(lua_State* L){return lua_tostring(L,lua_upvalueindex(2));}
bool Text(lua_State* L,int at,std::string* out){
 if(lua_type(L,at)!=LUA_TSTRING)return false;std::size_t n=0;const auto* p=lua_tolstring(L,at,&n);
 if(!n||n>96||std::memchr(p,0,n))return false;out->assign(p,n);return true;
}
bool Integer(lua_State* L,int at,std::uint64_t* out,std::uint64_t max=UINT32_MAX){
 if(lua_type(L,at)!=LUA_TNUMBER)return false;int ok=0;const auto v=lua_tointegerx(L,at,&ok);
 if(!ok||v<0||static_cast<std::uint64_t>(v)>max)return false;*out=static_cast<std::uint64_t>(v);return true;
}
bool Number(lua_State* L,int at,double* out){if(lua_type(L,at)!=LUA_TNUMBER)return false;*out=lua_tonumber(L,at);return std::isfinite(*out);}
int Result(lua_State* L,bool ok,const char* reason){lua_pushboolean(L,ok);lua_pushstring(L,ok?"ok":reason);return 2;}
void Field(lua_State* L,const char* key,std::uint64_t value){lua_pushinteger(L,static_cast<lua_Integer>(value));lua_setfield(L,-2,key);}
bool Context(lua_State* L){return H(L)&&H(L)->InLuaContext();}
int Available(lua_State* L){
 std::string name;const bool valid=lua_gettop(L)==1&&Text(L,1,&name);
 const unsigned flag=name=="dock"?CC2X_CARRIER_DOCK:name=="recovery"?CC2X_CARRIER_RECOVERY:0;
 lua_pushboolean(L,valid&&flag&&H(L)->Carrier().Available(flag));return 1;
}
int Request(lua_State* L){
 std::uint64_t carrier=0,vehicle=0;
 if(!Context(L)||lua_gettop(L)!=2||!Integer(L,1,&carrier)||!Integer(L,2,&vehicle))return Result(L,false,"invalid_context_or_ids");
 const auto id=H(L)->Carrier().Request(O(L),H(L)->Generation(),static_cast<std::uint32_t>(carrier),static_cast<std::uint32_t>(vehicle));
 lua_pushboolean(L,id!=0);lua_pushstring(L,id?"queued":"unavailable_or_request_refused");if(id){lua_pushinteger(L,id);return 3;}return 2;
}
int DockResult(lua_State* L){
 std::uint64_t request=0;cc2x_dock_result result{sizeof(result)};
 if(!Context(L)||lua_gettop(L)!=1||!Integer(L,1,&request,INT64_MAX)||!H(L)->Carrier().Result(O(L),request,&result)){
 lua_pushnil(L);lua_pushliteral(L,"unknown_request_or_context");return 2;}
 lua_newtable(L);Field(L,"request_id",result.request_id);Field(L,"generation",result.generation);
 Field(L,"carrier_id",result.carrier_id);Field(L,"vehicle_id",result.vehicle_id);Field(L,"code",result.code);
 constexpr const char* names[]{"pending","queued","already_queued","invalid_vehicle","profile_refused","engine_refused","engine_fault"};
 lua_pushstring(L,result.code<7?names[result.code]:"unknown");lua_setfield(L,-2,"status");return 1;
}
int Arm(lua_State* L){
 std::uint64_t carrier=0,vehicle=0;
 if(!Context(L)||lua_gettop(L)!=3||!Integer(L,1,&carrier)||!Integer(L,2,&vehicle)||!lua_istable(L,3))return Result(L,false,"invalid_context_or_arguments");
 cc2x_recovery_options options{sizeof(options),300,3,30};
 const char* names[]{"window_ticks","grants","touch_jumps"};std::uint32_t* fields[]{&options.window_ticks,&options.grants,&options.touch_jumps};
 for(int i=0;i<3;++i){lua_pushstring(L,names[i]);lua_rawget(L,3);std::uint64_t value=0;
 if(!lua_isnil(L,-1)){if(!Integer(L,-1,&value)){lua_pop(L,1);return Result(L,false,"invalid_recovery_budget");}*fields[i]=static_cast<std::uint32_t>(value);}lua_pop(L,1);}
 return Result(L,H(L)->Carrier().Arm(O(L),H(L)->Generation(),static_cast<std::uint32_t>(carrier),static_cast<std::uint32_t>(vehicle),options),"unavailable_or_recovery_refused");
}
int Disarm(lua_State* L){
 std::uint64_t vehicle=0;return Result(L,Context(L)&&lua_gettop(L)==1&&Integer(L,1,&vehicle)&&
 H(L)->Carrier().Disarm(O(L),static_cast<std::uint32_t>(vehicle)),"unknown_vehicle_owner_or_context");
}
int Status(lua_State* L){
 std::uint64_t vehicle=0;cc2x_recovery_status state{sizeof(state)};
 if(!Context(L)||lua_gettop(L)!=1||!Integer(L,1,&vehicle)||!H(L)->Carrier().Status(O(L),static_cast<std::uint32_t>(vehicle),&state)){
 lua_pushnil(L);lua_pushliteral(L,"unknown_vehicle_owner_or_context");return 2;}
 lua_newtable(L);Field(L,"vehicle_id",state.vehicle_id);Field(L,"carrier_id",state.carrier_id);Field(L,"generation",state.generation);
 Field(L,"grants",state.grants);Field(L,"touch_jumps",state.touch_jumps);Field(L,"remaining_ticks",state.remaining_ticks);Field(L,"reason",state.reason);
 lua_pushboolean(L,state.armed);lua_setfield(L,-2,"armed");return 1;
}
int EquipmentAvailable(lua_State* L){lua_pushboolean(L,Context(L)&&lua_gettop(L)==0&&H(L)->EquipmentAvailable());return 1;}
int EquipmentSet(lua_State* L){
 std::uint64_t vehicle=0,slot=0;int exact=0;std::string reason;
 const auto definition=lua_type(L,3)==LUA_TNUMBER?lua_tointegerx(L,3,&exact):0;
 if(!Context(L)||lua_gettop(L)!=4||!Integer(L,1,&vehicle)||!Integer(L,2,&slot,63)||!exact||definition< -1||definition>=102||lua_type(L,4)!=LUA_TBOOLEAN)
 return Result(L,false,"invalid_equipment_arguments_or_context");
 const bool ok=H(L)->SetVehicleAttachment(static_cast<std::uint32_t>(vehicle),static_cast<std::uint32_t>(slot),static_cast<std::int32_t>(definition),lua_toboolean(L,4)!=0,&reason);
 return Result(L,ok,reason.c_str());
}
int TileSites(lua_State* L){
 std::uint64_t tile=0,kind=0,definition=0;std::string reason;
 std::vector<cc2::api::world::TileSite> sites;
 if(!Context(L)||lua_gettop(L)!=3||!Integer(L,1,&tile)||!Integer(L,2,&kind,63)||!Integer(L,3,&definition,63)||
 !H(L)->ReadTileSites(static_cast<std::uint32_t>(tile),static_cast<std::uint32_t>(kind),static_cast<std::uint32_t>(definition),&sites,&reason)){
 lua_pushnil(L);lua_pushstring(L,reason.empty()?"invalid_tile_site_arguments":reason.c_str());return 2;}
 lua_newtable(L);int i=1;
 for(const auto& site:sites){lua_newtable(L);lua_newtable(L);int j=1;
 for(double value:site.transform){lua_pushnumber(L,value);lua_rawseti(L,-2,j++);}lua_setfield(L,-2,"transform");
 Field(L,"flags_low",site.flags_low);Field(L,"flags_high",site.flags_high);
 lua_pushnumber(L,site.transform[9]);lua_setfield(L,-2,"x");lua_pushnumber(L,site.transform[10]);lua_setfield(L,-2,"y");
 lua_pushnumber(L,site.transform[11]);lua_setfield(L,-2,"z");lua_rawseti(L,-2,i++);}
 return 1;
}
int SettingGet(lua_State* L){
 std::string key;auto* h=H(L);const auto args=lua_gettop(L);
 if(!Context(L)||args<2||args>4||!Text(L,1,&key)||!ModuleSettings::SafeName(key)){lua_pushnil(L);lua_pushliteral(L,"invalid_setting_arguments_or_context");return 2;}
 auto* settings=h->Settings(O(L));if(!settings){lua_pushnil(L);lua_pushliteral(L,"settings_unavailable");return 2;}
 if(lua_type(L,2)==LUA_TBOOLEAN&&args==2){bool value=lua_toboolean(L,2)!=0;settings->Boolean(key,&value);lua_pushboolean(L,value);return 1;}
 double fallback=0,min=-std::numeric_limits<double>::max(),max=std::numeric_limits<double>::max();
 if(!Number(L,2,&fallback)||(args>=3&&!lua_isnil(L,3)&&!Number(L,3,&min))||(args>=4&&!lua_isnil(L,4)&&!Number(L,4,&max))||min>max||fallback<min||fallback>max){
 lua_pushnil(L);lua_pushliteral(L,"expected_finite_default_and_bounds");return 2;}
 double value=fallback;settings->Number(key,&value);lua_pushnumber(L,std::clamp(value,min,max));return 1;
}
int SettingSet(lua_State* L){
 std::string key,reason;double value=0;
 if(!Context(L)||lua_gettop(L)!=2||!Text(L,1,&key)||!ModuleSettings::SafeName(key)||
 (lua_type(L,2)!=LUA_TBOOLEAN&&!Number(L,2,&value)))return Result(L,false,"expected_key_and_finite_number_or_boolean");
 auto* s=H(L)->Settings(O(L));if(!s||!H(L)->SettingsIo(O(L)))return Result(L,false,"settings_unavailable_or_rate_limited");
 const bool ok=lua_type(L,2)==LUA_TBOOLEAN?s->SetBoolean(key,lua_toboolean(L,2)!=0,&reason):s->SetNumber(key,value,&reason);
 return Result(L,ok,reason.c_str());
}
int SettingReload(lua_State* L){
 std::string reason;if(!Context(L)||lua_gettop(L)!=0)return Result(L,false,"invalid_context_or_arguments");
 auto* s=H(L)->Settings(O(L));if(!s||!H(L)->SettingsIo(O(L)))return Result(L,false,"settings_unavailable_or_rate_limited");
 const bool ok=s->Reload(&reason);return Result(L,ok,reason.c_str());
}
}
void RegisterModuleExtensions(lua_State* L,Host* host,const std::string& owner){
 auto add=[&](const char* name,lua_CFunction fn){lua_pushlightuserdata(L,host);lua_pushlstring(L,owner.data(),owner.size());lua_pushcclosure(L,fn,2);lua_setglobal(L,name);};
 add("cc2_bridge_carrier_available",Available);add("cc2_bridge_carrier_dock_request",Request);add("cc2_bridge_carrier_dock_result",DockResult);
 add("cc2_bridge_carrier_recovery_arm",Arm);add("cc2_bridge_carrier_recovery_disarm",Disarm);add("cc2_bridge_carrier_recovery_status",Status);
 add("cc2_bridge_tile_sites",TileSites);
 add("cc2_bridge_equipment_available",EquipmentAvailable);add("cc2_bridge_equipment_set",EquipmentSet);
 add("cc2_bridge_settings_get",SettingGet);add("cc2_bridge_settings_set",SettingSet);add("cc2_bridge_settings_reload",SettingReload);
}
ModuleSettings* Host::Settings(const std::string& owner){
 std::lock_guard<std::recursive_mutex> l(mutex_);
 if(!InServerContext()||!ModuleSettings::SafeName(owner)||(owner.compare(0,4,"lua.")&&owner.compare(0,7,"native.")))return nullptr;
 auto found=settings_.find(owner);if(found!=settings_.end())return found->second.get();
 if(settings_.size()>=256)return nullptr;
 auto store=std::make_unique<ModuleSettings>(std::filesystem::path(storage_).parent_path().wstring(),owner);
 std::string reason;if(!store->Reload(&reason))Log("SETTINGS_READ_REFUSED",owner.c_str(),reason.c_str());
 auto* result=store.get();settings_.emplace(owner,std::move(store));return result;
}
bool Host::SettingsIo(const std::string& owner){
 std::lock_guard<std::recursive_mutex> l(mutex_);if(!InServerContext()||!settings_.count(owner))return false;
 auto& b=settingsBudgets_[owner];const auto second=GetTickCount64()/1000;if(b.second!=second){b.second=second;b.count=0;}
 if(b.count>=8)return false;++b.count;return true;
}
}
namespace {
using cc2::runtime::SharedHost;
bool Owner(const char* p,std::string* owner){if(!p)return false;const auto n=strnlen(p,97);if(n<=7||n>96||std::strncmp(p,"native.",7))return false;
 owner->assign(p,n);return cc2::runtime::ModuleSettings::SafeName(*owner);}
int Available(std::uint32_t c){auto* h=SharedHost();return h&&h->Carrier().Available(c);}
std::uint64_t Request(const char* o,std::uint64_t g,std::uint32_t c,std::uint32_t v){auto* h=SharedHost();std::string owner;return h&&Owner(o,&owner)?h->Carrier().Request(owner,g,c,v):0;}
int Result(const char* o,std::uint64_t id,cc2x_dock_result* result){auto* h=SharedHost();std::string owner;return h&&Owner(o,&owner)&&h->Carrier().Result(owner,id,result);}
int Arm(const char* o,std::uint64_t g,std::uint32_t c,std::uint32_t v,const cc2x_recovery_options* options){auto* h=SharedHost();std::string owner;return h&&options&&Owner(o,&owner)&&h->Carrier().Arm(owner,g,c,v,*options);}
int Disarm(const char* o,std::uint32_t v){auto* h=SharedHost();std::string owner;return h&&Owner(o,&owner)&&h->Carrier().Disarm(owner,v);}
int Status(const char* o,std::uint32_t v,cc2x_recovery_status* result){auto* h=SharedHost();std::string owner;return h&&Owner(o,&owner)&&h->Carrier().Status(owner,v,result);}
cc2::runtime::ModuleSettings* Settings(const char* o,const char* k,std::string* key){
 auto* h=SharedHost();std::string owner;if(!h||!Owner(o,&owner)||!k)return nullptr;auto n=strnlen(k,97);
 if(!n||n>96)return nullptr;key->assign(k,n);return cc2::runtime::ModuleSettings::SafeName(*key)?h->Settings(owner):nullptr;
}
int Number(const char* o,const char* k,double fallback,double min,double max,double* out){
 if(!out||!std::isfinite(fallback)||!std::isfinite(min)||!std::isfinite(max)||min>max||fallback<min||fallback>max)return 0;
 std::string key;auto* s=Settings(o,k,&key);if(!s)return 0;double value=fallback;s->Number(key,&value);*out=std::clamp(value,min,max);return 1;
}
int Boolean(const char* o,const char* k,int fallback,int* out){
 if(!out||(fallback!=0&&fallback!=1))return 0;std::string key;auto* s=Settings(o,k,&key);if(!s)return 0;
 bool value=fallback!=0;s->Boolean(key,&value);*out=value?1:0;return 1;
}
int SetNumber(const char* o,const char* k,double v){std::string key,reason;auto* s=Settings(o,k,&key);return s&&SharedHost()->SettingsIo(o)&&s->SetNumber(key,v,&reason);}
int SetBoolean(const char* o,const char* k,int v){std::string key,reason;auto* s=Settings(o,k,&key);return (v==0||v==1)&&s&&SharedHost()->SettingsIo(o)&&s->SetBoolean(key,v!=0,&reason);}
int Reload(const char* o){auto* h=SharedHost();std::string owner,reason;if(!h||!Owner(o,&owner))return 0;auto* s=h->Settings(owner);return s&&h->SettingsIo(owner)&&s->Reload(&reason);}
}
extern "C" CC2X_EXPORT const cc2x_carrier_api* cc2x_query_carrier_v1(std::uint32_t version,std::uint32_t size){
 static const cc2x_carrier_api api{CC2X_CARRIER_VERSION,sizeof(api),CC2X_CARRIER_ID,Available,Request,Result,Arm,Disarm,Status};
 return version==CC2X_CARRIER_VERSION&&size==sizeof(api)?&api:nullptr;
}
extern "C" CC2X_EXPORT const cc2x_settings_api* cc2x_query_settings_v1(std::uint32_t version,std::uint32_t size){
 static const cc2x_settings_api api{CC2X_SETTINGS_VERSION,sizeof(api),CC2X_SETTINGS_ID,Number,Boolean,SetNumber,SetBoolean,Reload};
 return version==CC2X_SETTINGS_VERSION&&size==sizeof(api)?&api:nullptr;
}

namespace {
int EquipmentAvailableC(){auto* h=SharedHost();return h&&h->EquipmentAvailable();}
int EquipmentSetC(std::uint32_t v,std::uint32_t slot,std::int32_t definition,int fill){
 auto* h=SharedHost();std::string reason;return h&&(fill==0||fill==1)&&h->SetVehicleAttachment(v,slot,definition,fill!=0,&reason);
}
}
extern "C" CC2X_EXPORT const cc2x_equipment_api* cc2x_query_equipment_v1(std::uint32_t version,std::uint32_t size){
 static const cc2x_equipment_api api{CC2X_EQUIPMENT_VERSION,sizeof(api),CC2X_EQUIPMENT_ID,EquipmentAvailableC,EquipmentSetC};
 return version==CC2X_EQUIPMENT_VERSION&&size==sizeof(api)?&api:nullptr;
}

namespace {
int ReadTileSitesC(std::uint32_t id,std::uint32_t kind,std::uint32_t definition,cc2x_tile_site* out,std::uint32_t capacity,std::uint32_t* count){
 auto* h=SharedHost();if(!h||!count||capacity>64||(!out&&capacity))return 0;*count=0;
 std::vector<cc2::api::world::TileSite> sites;std::string reason;
 if(!h->ReadTileSites(id,kind,definition,&sites,&reason))return 0;*count=static_cast<std::uint32_t>(sites.size());
 if(capacity<sites.size())return 0;
 for(std::size_t i=0;i<sites.size();++i){std::memcpy(out[i].transform,sites[i].transform.data(),sizeof(out[i].transform));out[i].flags_low=sites[i].flags_low;out[i].flags_high=sites[i].flags_high;}return 1;
}
}
extern "C" CC2X_EXPORT const cc2x_tile_sites_api* cc2x_query_tile_sites_v1(std::uint32_t version,std::uint32_t size){
 static const cc2x_tile_sites_api api{CC2X_TILE_SITES_VERSION,sizeof(api),CC2X_TILE_SITES_ID,ReadTileSitesC};
 return version==CC2X_TILE_SITES_VERSION&&size==sizeof(api)?&api:nullptr;
}
