#include "early_declaration.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <Windows.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <set>

namespace cc2::host {
namespace {

struct Context {
  const ModDescriptor* mod=nullptr;
  std::vector<VehicleDeclaration> pending;
  std::size_t allocated=0;
  unsigned instructions=0;
};

std::wstring Wide(const char* value) {
  if (!value) return {};
  const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value,-1,nullptr,0);
  if (!count) return {};
  std::wstring result(static_cast<std::size_t>(count),L'\0');
  if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value,-1,result.data(),count)) return {};
  result.pop_back();
  return result;
}

std::string Utf8(const std::wstring& value) {
  const int count=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
  std::string result(static_cast<std::size_t>(count),'\0');
  if (count) WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),count,nullptr,nullptr);
  return result;
}

bool SafeLocalId(const std::wstring& id) {
  if (id.empty() || id.size()>64) return false;
  for (wchar_t c:id)
    if (!((c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||
          (c>=L'0'&&c<=L'9')||c==L'_'||c==L'-')) return false;
  return true;
}

std::wstring StringField(lua_State* L,int table,const char* field) {
  lua_getfield(L,table,field);
  const char* value=lua_type(L,-1)==LUA_TSTRING?lua_tostring(L,-1):nullptr;
  std::wstring result=Wide(value);
  lua_pop(L,1);
  return result;
}

bool HasField(lua_State* L,int table,const char* field) {
  lua_getfield(L,table,field);
  const bool found=!lua_isnil(L,-1);
  lua_pop(L,1);
  return found;
}

std::string RegisterVehicleImpl(lua_State* L,Context* context) {
  if (!context || lua_gettop(L)!=1 || !lua_istable(L,1)) return "register expects one descriptor table";
  auto id=StringField(L,1,"id");
  auto name=StringField(L,1,"name");
  auto base=StringField(L,1,"base");
  auto definition=StringField(L,1,"definition");
  auto ui=StringField(L,1,"ui");
  std::uint32_t order=0xffffffffu;
  lua_getfield(L,1,"order");
  if (!lua_isnil(L,-1)) {
    int valid=0;
    const auto value=lua_tointegerx(L,-1,&valid);
    if (!valid || value<0 || value>65535) {
      lua_pop(L,1);
      return "order must be an integer from 0 through 65535";
    }
    order=static_cast<std::uint32_t>(value);
  }
  lua_pop(L,1);
  for (const auto& prior:context->pending)
    if (_wcsicmp(prior.localId.c_str(),id.c_str())==0) return "duplicate vehicle id";
  VehicleDeclaration declared;
  std::string error;
  if(!BuildVehicleDeclaration(*context->mod,{id,name,base,definition,ui,order,HasField(L,1,"ui")},&declared,&error))return error;
  context->pending.push_back(std::move(declared));
  return {};
}

int RegisterVehicle(lua_State* L) {
  auto* context=static_cast<Context*>(lua_touserdata(L,lua_upvalueindex(1)));
  bool invalid=false;
  {
    const auto error=RegisterVehicleImpl(L,context);
    invalid=!error.empty();
    if (invalid) lua_pushlstring(L,error.data(),error.size());
  }
  if (invalid) return lua_error(L);
  lua_pushstring(L,Utf8(context->pending.back().identity).c_str());
  return 1;
}

int Require(lua_State* L) {
  if (lua_gettop(L)!=1 || !lua_isstring(L,1) ||
      std::string(lua_tostring(L,1))!="cc2") return luaL_error(L,"early require supports cc2 only");
  lua_getglobal(L,"cc2");
  return 1;
}

void* Allocate(void* user,void* pointer,std::size_t previous,std::size_t next) {
  auto* context=static_cast<Context*>(user);
  if (!pointer) previous=0;
  if (!next) {std::free(pointer);context->allocated-=previous;return nullptr;}
  constexpr std::size_t limit=8u*1024u*1024u;
  if (next>limit || context->allocated-previous>limit-next) return nullptr;
  void* result=std::realloc(pointer,next);
  if (result) context->allocated=context->allocated-previous+next;
  return result;
}

void Budget(lua_State* L,lua_Debug*) {
  auto* context=*static_cast<Context**>(lua_getextraspace(L));
  if (++context->instructions>200) luaL_error(L,"early declaration instruction budget exceeded");
}

void Hide(lua_State* L,const char* name) {lua_pushnil(L);lua_setglobal(L,name);}

bool RunMod(const ModDescriptor& mod,std::vector<VehicleDeclaration>* output,std::wstring* error) {
  Context context;context.mod=&mod;
  lua_State* L=lua_newstate(Allocate,&context);
  if (!L) {*error=L"early Lua allocation failed";return false;}
  *static_cast<Context**>(lua_getextraspace(L))=&context;
  luaL_requiref(L,"_G",luaopen_base,1);lua_pop(L,1);
  luaL_requiref(L,LUA_TABLIBNAME,luaopen_table,1);lua_pop(L,1);
  luaL_requiref(L,LUA_STRLIBNAME,luaopen_string,1);lua_pop(L,1);
  luaL_requiref(L,LUA_MATHLIBNAME,luaopen_math,1);lua_pop(L,1);
  Hide(L,"dofile");Hide(L,"loadfile");Hide(L,"load");Hide(L,"collectgarbage");
  lua_pushcfunction(L,Require);lua_setglobal(L,"require");
  lua_newtable(L);lua_newtable(L);
  lua_pushlightuserdata(L,&context);lua_pushcclosure(L,RegisterVehicle,1);
  lua_setfield(L,-2,"register");lua_setfield(L,-2,"vehicles");
  lua_pushvalue(L,-1);lua_setglobal(L,"cc2");lua_pop(L,1);
  lua_sethook(L,Budget,LUA_MASKCOUNT,10000);
  bool ok=true;
  for (const auto& script:mod.bridgeScripts) {
    std::wstring path;
    if (!ResolveModAssetPath(mod,script,L".lua",&path)) {*error=L"early script path became unsafe";ok=false;break;}
    std::ifstream file(std::filesystem::path(path),std::ios::binary|std::ios::ate);
    if (!file || file.tellg()<0 || file.tellg()>1024*1024) {*error=L"early script unreadable or exceeds 1 MiB";ok=false;break;}
    std::string source(static_cast<std::size_t>(file.tellg()),'\0');
    file.seekg(0);if (!source.empty()) file.read(source.data(),source.size());
    if (!file) {*error=L"early script read failed";ok=false;break;}
    const auto chunk=Utf8(path);
    int rc=luaL_loadbufferx(L,source.data(),source.size(),chunk.c_str(),"t");
    if (rc==LUA_OK) rc=lua_pcall(L,0,1,0);
    if (rc!=LUA_OK) {*error=Wide(lua_tostring(L,-1));ok=false;break;}
    lua_pop(L,1);
  }
  if (ok) *output=std::move(context.pending);
  lua_close(L);
  return ok;
}

}  // namespace

bool BuildVehicleDeclaration(const ModDescriptor& mod,const VehicleFields& f,VehicleDeclaration* out,std::string* error) {
  auto fail=[&](const char* reason){if(error)*error=reason;return false;};
  if(!out||!SafeLocalId(f.id)||f.name.empty()||f.name.size()>128||f.base.empty()||f.definition.empty())return fail("invalid id, name, base or definition");
  if(f.order!=UINT32_MAX&&f.order>65535)return fail("order must be an integer from 0 through 65535");
  if(f.hasUi&&(!SafeLocalId(f.ui)||std::none_of(mod.uiImages.begin(),mod.uiImages.end(),[&](const auto& image){return !image.weapon&&image.identity==f.ui;})))return fail("ui must reference a declared vehicle image");
  std::uint32_t base=0;
  if(f.base==L"wing_light")base=8;else if(f.base==L"wing_heavy")base=10;else return fail("unsupported base class (verified: wing_light, wing_heavy)");
  std::wstring path;
  if(f.definition.rfind(L"game_objects/",0)!=0||f.definition.find(L'/',13)!=std::wstring::npos||!ResolveModAssetPath(mod,f.definition,L".xml",&path))return fail("definition must be an existing safe game_objects/*.xml path inside the mod");
  auto resource=f.definition.substr(f.definition.find_last_of(L'/')+1);resource.resize(resource.size()-4);
  if(!SafeLocalId(resource))return fail("invalid definition resource name");
  VehicleDeclaration result;result.owner=mod.id;result.localId=f.id;result.identity=mod.id+L"."+f.id;
  std::transform(result.identity.begin(),result.identity.end(),result.identity.begin(),towlower);
  result.name=f.name;result.definitionPath=path;result.resourceName=resource;result.ui=f.ui;result.nativeBaseType=base;result.order=f.order;
  *out=std::move(result);if(error)error->clear();return true;
}
bool BuildVehicleDeclarationFromC(const ModDescriptor& mod,const cc2x_vehicle_declaration& f,VehicleDeclaration* out,std::string* error) {
  if(f.size!=sizeof(f)){if(error)*error="invalid C declaration size";return false;}
  auto field=[](const char* value){if(!value||strnlen(value,1025)>1024)return std::wstring{};return Wide(value);};
  return BuildVehicleDeclaration(mod,{field(f.id),field(f.name),field(f.base),field(f.definition),field(f.ui),f.order,f.ui!=nullptr},out,error);
}

DeclarationCatalog CollectEarlyDeclarations(const ModDiscovery& discovery) {
  DeclarationCatalog catalog;
  std::set<std::wstring> resources;
  for (const auto& mod:discovery.mods) {
    if (mod.schema<2) continue;
    std::vector<VehicleDeclaration> pending;
    std::wstring error;
    if (!RunMod(mod,&pending,&error)) {
      catalog.diagnostics.push_back(mod.id+L": EARLY_DECLARATION_FAILED: "+error);
      continue;
    }
    bool duplicate=false;
    std::set<std::wstring> pendingResources;
    for (const auto& vehicle:pending) {
      std::wstring key=vehicle.resourceName;
      std::transform(key.begin(),key.end(),key.begin(),towlower);
      if (resources.count(key) || !pendingResources.insert(key).second) duplicate=true;
    }
    if (duplicate) {
      catalog.diagnostics.push_back(mod.id+L": duplicate engine definition resource name");
      continue;
    }
    resources.insert(pendingResources.begin(),pendingResources.end());
    for (auto& vehicle:pending) catalog.vehicles.push_back(std::move(vehicle));
  }
  return catalog;
}

}  // namespace cc2::host
