extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include <cstdint>
#include <iostream>
#include <string>

namespace {
int spawnCalls=0;
std::string lastIdentity;
lua_Integer lastCarrier=0,lastSlot=0;

int Spawn(lua_State* state) {
  if(lua_gettop(state)!=3||lua_type(state,1)!=LUA_TSTRING)return 0;
  ++spawnCalls;
  lastIdentity=lua_tostring(state,1);
  lastCarrier=lua_tointeger(state,2);
  lastSlot=lua_tointeger(state,3);
  if(lastIdentity=="example.aircraft:not_ready") {
    lua_pushnil(state);lua_pushliteral(state,"vehicle_type_not_ready");return 2;
  }
  lua_pushinteger(state,501);return 1;
}

int SpawnResult(lua_State* state) {
  if(lua_gettop(state)!=1||lua_tointeger(state,1)!=501)return 0;
  lua_createtable(state,0,3);
  lua_pushinteger(state,501);lua_setfield(state,-2,"request_id");
  lua_pushboolean(state,1);lua_setfield(state,-2,"success");
  lua_pushinteger(state,9001);lua_setfield(state,-2,"vehicle_id");
  return 1;
}

bool Run(lua_State* state,const char* source) {
  if(luaL_loadstring(state,source)!=LUA_OK||lua_pcall(state,0,0,0)!=LUA_OK) {
    const char* error=lua_tostring(state,-1);
    std::cerr<<(error?error:"unknown Lua error")<<"\n";
    return false;
  }
  return true;
}
}

int main(int argc,char** argv) {
  if(argc!=2)return 2;
  lua_State* state=luaL_newstate();
  if(!state)return 3;
  luaL_openlibs(state);
  lua_pushcfunction(state,Spawn);lua_setglobal(state,"cc2_bridge_vehicle_spawn_in_hangar");
  lua_pushcfunction(state,SpawnResult);lua_setglobal(state,"cc2_bridge_hangar_spawn_result");
  if(luaL_loadfilex(state,argv[1],"t")!=LUA_OK||lua_pcall(state,0,1,0)!=LUA_OK) {
    const char* error=lua_tostring(state,-1);
    std::cerr<<(error?error:"failed to load SDK")<<"\n";
    lua_close(state);return 4;
  }
  lua_pop(state,1);

  const bool passed=Run(state,R"lua(
    local request, reason = cc2.vehicles.spawn_in_hangar({
      type = "example.aircraft:f16", carrier = 17, slot = 2
    })
    assert(request == 501 and reason == nil)
    local result = cc2.vehicles.hangar_spawn_result(request)
    assert(result.request_id == 501 and result.success and result.vehicle_id == 9001)
    local bad, why = cc2.vehicles.spawn_in_hangar({
      type = "example.aircraft:f16", carrier = 0, slot = 2
    })
    assert(bad == nil and why == "expected_type_identity_carrier_id_and_slot")
    local missing, missingWhy = cc2.vehicles.spawn_in_hangar({
      type = "example.aircraft:not_ready", carrier = 17, slot = 2
    })
    assert(missing == nil and missingWhy == "vehicle_type_not_ready")
  )lua");
  const bool marshalled=spawnCalls==2&&lastIdentity=="example.aircraft:not_ready"&&
      lastCarrier==17&&lastSlot==2;
  lua_close(state);
  if(!passed||!marshalled)return 1;
  std::cout<<"PASS typed vehicle spawn SDK validation, identity forwarding, and shared structured result\n";
  return 0;
}
