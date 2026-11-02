extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

namespace {
struct Scenario {
  const char* identity = nullptr;
  std::uint32_t definitionId = 0;
  int triggerKey = 0;
  int downKey = 0;
  int spawnCalls = 0;
  int resultCalls = 0;
  int requestId = 0;
  std::uint32_t vehicleId = 0;
  std::string submittedIdentity;
  std::uint32_t carrierId = 0;
  std::uint32_t slot = 0;
  bool resultReady = false;
  bool argumentMismatch = false;
};

Scenario g_scenario;

void SetInteger(lua_State* state, const char* key, lua_Integer value) {
  lua_pushinteger(state, value);
  lua_setfield(state, -2, key);
}

int RegisterVehicle(lua_State* state) {
  if (lua_gettop(state) != 1 || !lua_istable(state, 1)) return 0;
  lua_getfield(state, 1, "name");
  const char* name = lua_tostring(state, -1);
  const bool alpha = name && std::strcmp(name, "Alpha Fighter") == 0;
  lua_pop(state, 1);
  lua_pushstring(state, alpha ? "cc2x.identity.alpha.fighter"
                              : "cc2x.identity.beta.fighter");
  return 1;
}

int LookupVehicle(lua_State* state) {
  const char* identity = lua_type(state, 1) == LUA_TSTRING
      ? lua_tostring(state, 1) : nullptr;
  if (!identity || std::strcmp(identity, g_scenario.identity) != 0) {
    lua_pushnil(state);
    return 1;
  }
  lua_createtable(state, 0, 4);
  SetInteger(state, "definition_id", g_scenario.definitionId);
  lua_pushboolean(state, 1); lua_setfield(state, -2, "ready");
  lua_pushstring(state, g_scenario.identity); lua_setfield(state, -2, "identity");
  return 1;
}

int WorldVehicles(lua_State* state) {
  lua_createtable(state, 1, 0);
  lua_createtable(state, 0, 3);
  SetInteger(state, "id", 2);
  SetInteger(state, "definition", 0);
  lua_pushboolean(state, 0); lua_setfield(state, -2, "team_is_ai");
  lua_rawseti(state, -2, 1);
  return 1;
}

int HangarSlots(lua_State* state) {
  int carrierOk = 0, definitionOk = 0;
  const auto carrier = lua_tointegerx(state, 1, &carrierOk);
  const auto definition = lua_tointegerx(state, 2, &definitionOk);
  if (!carrierOk || !definitionOk || carrier != 2 ||
      definition != g_scenario.definitionId) {
    g_scenario.argumentMismatch = true;
    lua_pushnil(state);
    lua_pushliteral(state, "invalid_hangar_slot_query");
    return 2;
  }
  lua_createtable(state, 2, 0);
  lua_createtable(state, 0, 3);
  SetInteger(state, "index", 3);
  lua_pushboolean(state, 0); lua_setfield(state, -2, "compatible");
  lua_pushboolean(state, 0); lua_setfield(state, -2, "occupied");
  lua_rawseti(state, -2, 1);
  lua_createtable(state, 0, 3);
  SetInteger(state, "index", 8);
  lua_pushboolean(state, 1); lua_setfield(state, -2, "compatible");
  lua_pushboolean(state, 0); lua_setfield(state, -2, "occupied");
  lua_rawseti(state, -2, 2);
  return 1;
}

int SpawnVehicle(lua_State* state) {
  ++g_scenario.spawnCalls;
  if (lua_gettop(state) != 1 || !lua_istable(state, 1)) {
    g_scenario.argumentMismatch = true;
    lua_pushnil(state);
    lua_pushliteral(state, "expected_options_table");
    return 2;
  }
  lua_getfield(state, 1, "type");
  const char* identity = lua_tostring(state, -1);
  g_scenario.submittedIdentity = identity ? identity : "";
  lua_pop(state, 1);
  lua_getfield(state, 1, "carrier");
  int carrierOk = 0;
  const auto carrier = lua_tointegerx(state, -1, &carrierOk);
  lua_pop(state, 1);
  lua_getfield(state, 1, "slot");
  int slotOk = 0;
  const auto slot = lua_tointegerx(state, -1, &slotOk);
  lua_pop(state, 1);
  if (g_scenario.submittedIdentity != g_scenario.identity || !carrierOk ||
      carrier != 2 || !slotOk || slot != 8) {
    g_scenario.argumentMismatch = true;
  }
  g_scenario.carrierId = carrierOk ? static_cast<std::uint32_t>(carrier) : 0;
  g_scenario.slot = slotOk ? static_cast<std::uint32_t>(slot) : UINT32_MAX;
  g_scenario.requestId = g_scenario.definitionId == 105 ? 601 : 602;
  lua_pushinteger(state, g_scenario.requestId);
  return 1;
}

int SpawnResult(lua_State* state) {
  ++g_scenario.resultCalls;
  int requestOk = 0;
  const auto request = lua_tointegerx(state, 1, &requestOk);
  if (!requestOk || request != g_scenario.requestId) {
    g_scenario.argumentMismatch = true;
    lua_pushnil(state);
    lua_pushliteral(state, "wrong_request_id");
    return 2;
  }
  if (!g_scenario.resultReady) {
    lua_pushnil(state);
    lua_pushliteral(state, "pending_or_expired");
    return 2;
  }
  lua_createtable(state, 0, 4);
  SetInteger(state, "request_id", g_scenario.requestId);
  lua_pushboolean(state, 1); lua_setfield(state, -2, "success");
  SetInteger(state, "vehicle_id", g_scenario.vehicleId);
  SetInteger(state, "error_code", 0);
  return 1;
}

int KeyDown(lua_State* state) {
  int keyOk = 0;
  const auto key = lua_tointegerx(state, 1, &keyOk);
  lua_pushboolean(state, keyOk && key == g_scenario.downKey);
  return 1;
}

int Print(lua_State*) { return 0; }

void SetFunction(lua_State* state, const char* name, lua_CFunction function) {
  lua_pushcfunction(state, function);
  lua_setfield(state, -2, name);
}

void InstallRuntime(lua_State* state) {
  lua_newtable(state);  // cc2
  lua_newtable(state);  // cc2.vehicles
  SetFunction(state, "register", RegisterVehicle);
  SetFunction(state, "lookup", LookupVehicle);
  SetFunction(state, "spawn_in_hangar", SpawnVehicle);
  SetFunction(state, "hangar_spawn_result", SpawnResult);
  lua_setfield(state, -2, "vehicles");

  lua_newtable(state);  // cc2.world
  SetFunction(state, "vehicles", WorldVehicles);
  lua_setfield(state, -2, "world");

  lua_newtable(state);  // cc2.carrier
  SetFunction(state, "hangar_slots", HangarSlots);
  lua_setfield(state, -2, "carrier");

  lua_newtable(state);  // cc2.input
  SetFunction(state, "key_down", KeyDown);
  lua_setfield(state, -2, "input");
  lua_setglobal(state, "cc2");
  lua_pushcfunction(state, Print);
  lua_setglobal(state, "print");
}

bool Call(lua_State* state, int module, const char* method, bool argument) {
  lua_getfield(state, module, method);
  if (!lua_isfunction(state, -1)) {
    lua_pop(state, 1);
    return false;
  }
  if (argument) lua_pushnumber(state, 0.016);
  return lua_pcall(state, argument ? 1 : 0, 0, 0) == LUA_OK;
}

bool RunFixture(const char* path, const char* identity, std::uint32_t definitionId,
                int triggerKey, std::uint32_t vehicleId, int otherKey) {
  g_scenario = {};
  g_scenario.identity = identity;
  g_scenario.definitionId = definitionId;
  g_scenario.triggerKey = triggerKey;
  g_scenario.vehicleId = vehicleId;
  lua_State* state = luaL_newstate();
  if (!state) return false;
  luaL_openlibs(state);
  InstallRuntime(state);
  if (luaL_loadfilex(state, path, "t") != LUA_OK ||
      lua_pcall(state, 0, 1, 0) != LUA_OK || !lua_istable(state, -1)) {
    lua_close(state);
    return false;
  }
  const int module = lua_absindex(state, -1);
  bool passed = Call(state, module, "on_load", false);
  g_scenario.downKey = 0;
  passed = passed && Call(state, module, "on_update", true) && g_scenario.spawnCalls == 0;
  g_scenario.downKey = otherKey;
  passed = passed && Call(state, module, "on_update", true) && g_scenario.spawnCalls == 0;
  g_scenario.downKey = triggerKey;
  passed = passed && Call(state, module, "on_update", true) && g_scenario.spawnCalls == 1;
  passed = passed && g_scenario.submittedIdentity == identity &&
      g_scenario.carrierId == 2 && g_scenario.slot == 8 && !g_scenario.argumentMismatch;
  passed = passed && Call(state, module, "on_update", true) && g_scenario.spawnCalls == 1;
  g_scenario.downKey = 0;
  passed = passed && Call(state, module, "on_update", true) && g_scenario.spawnCalls == 1;
  g_scenario.resultReady = true;
  passed = passed && Call(state, module, "on_update", true) && g_scenario.spawnCalls == 1;
  passed = passed && g_scenario.resultCalls >= 3 && !g_scenario.argumentMismatch;
  lua_close(state);
  return passed;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  const bool alpha = RunFixture(argv[1], "cc2x.identity.alpha.fighter", 105,
      0x79, 2301, 0x7A);
  const bool beta = RunFixture(argv[2], "cc2x.identity.beta.fighter", 106,
      0x7A, 2302, 0x79);
  if (!alpha || !beta) {
    std::cerr << "FAIL multi-mod typed spawn remains manual, owner-scoped, and slot-validated\n";
    return 1;
  }
  std::cout << "PASS F10/F11 manually resolve distinct typed identities and results; no automatic spawn\n";
  return 0;
}
