extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include <cstdio>
#include <cstdint>
#include <string>

namespace {
int LegacyAircraftTypes(lua_State* L) {
  lua_createtable(L, 2, 0);
  const struct { int id; const char* name; int base; } defs[] = {
      {102, "F-16", 8}, {103, "F-15", 10}};
  for (int i = 0; i < 2; ++i) {
    lua_createtable(L, 0, 6);
    lua_pushinteger(L, defs[i].id); lua_setfield(L, -2, "id");
    lua_pushstring(L, defs[i].name); lua_setfield(L, -2, "name");
    lua_pushinteger(L, defs[i].base); lua_setfield(L, -2, "base_type");
    lua_pushboolean(L, 1); lua_setfield(L, -2, "aircraft");
    lua_pushinteger(L, i == 0 ? 0x40000001 : 0x40000002); lua_setfield(L, -2, "icon16");
    lua_pushinteger(L, i == 0 ? 0x40000003 : 0x40000004); lua_setfield(L, -2, "loadout64");
    lua_rawseti(L, -2, i + 1);
  }
  return 1;
}

int VehicleDefinitions(lua_State* L) {
  lua_createtable(L, 1, 0);
  lua_createtable(L, 0, 8);
  lua_pushinteger(L, 104); lua_setfield(L, -2, "id");
  lua_pushliteral(L, "cc2x.dynamic.vehicle.test.third_aircraft"); lua_setfield(L, -2, "identity");
  lua_pushliteral(L, "Third aircraft definition test"); lua_setfield(L, -2, "name");
  lua_pushinteger(L, 8); lua_setfield(L, -2, "base_type");
  lua_pushboolean(L, 1); lua_setfield(L, -2, "aircraft");
  lua_pushboolean(L, 1); lua_setfield(L, -2, "ready");
  lua_pushinteger(L, 0x40000005); lua_setfield(L, -2, "icon16");
  lua_pushinteger(L, 0x40000006); lua_setfield(L, -2, "icon64");
  lua_pushinteger(L, 0x40000007); lua_setfield(L, -2, "loadout64");
  lua_rawseti(L, -2, 1);
  return 1;
}

int BeginGetUiRegionIndex(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  if (std::string(name) == "f16") lua_pushinteger(L, 1016);
  else if (std::string(name) == "f15") lua_pushinteger(L, 1015);
  else if (std::string(name) == "f16_layout") lua_pushinteger(L, 1032);
  else if (std::string(name) == "f15_layout") lua_pushinteger(L, 1033);
  else lua_pushinteger(L, -1);
  return 1;
}

int Iff(lua_State* L) {
  lua_pushvalue(L, lua_toboolean(L, 1) ? 2 : 3);
  return 1;
}

int StockChassisData(lua_State* L) {
  lua_pushliteral(L, "stock");
  lua_pushinteger(L, 77);
  lua_pushliteral(L, "stk");
  lua_pushliteral(L, "stock description");
  return 4;
}

void SetInteger(lua_State* L, const char* name, lua_Integer value) {
  lua_pushinteger(L, value);
  lua_setfield(L, -2, name);
}

bool CheckOptions(lua_State* L, int bay, int expectedCount) {
  lua_getglobal(L, "get_selected_chassis_options");
  lua_pushinteger(L, bay);
  if (lua_pcall(L, 1, 1, 0) != LUA_OK || !lua_istable(L, -1)) return false;
  if (lua_rawlen(L, -1) != static_cast<std::size_t>(expectedCount)) return false;
  if (bay >= 8) {
    const int expectedIds[] = {-1, 8, 10, 12, 14, 104, 102, 103};
    for (int i = 0; i < expectedCount; ++i) {
      lua_rawgeti(L, -1, i + 1);
      lua_getfield(L, -1, "type");
      const auto actual = lua_tointeger(L, -1);
      lua_pop(L, 1);
      if (actual != expectedIds[i]) { lua_pop(L, 1); return false; }
      if (i >= 5) {
        lua_getfield(L, -1, "region");
        const auto expectedRegion = i == 5 ? 0x40000005 : i == 6 ? 0x40000001 : 0x40000002;
        const bool regionMatches = lua_tointeger(L, -1) == expectedRegion;
        lua_pop(L, 1);
        lua_pop(L, 1);
        if (!regionMatches) return false;
      } else {
        lua_pop(L, 1);
      }
    }
  }
  lua_pop(L, 1);
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  lua_State* L = luaL_newstate();
  if (!L) return 3;
  luaL_openlibs(L);

  lua_newtable(L);
  SetInteger(L, "count", 102);
  SetInteger(L, "chassis_air_wing_light", 8);
  SetInteger(L, "chassis_air_wing_heavy", 10);
  SetInteger(L, "chassis_air_rotor_light", 12);
  SetInteger(L, "chassis_air_rotor_heavy", 14);
  SetInteger(L, "chassis_land_wheel_light", 2);
  SetInteger(L, "chassis_land_wheel_medium", 4);
  SetInteger(L, "chassis_land_wheel_heavy", 6);
  SetInteger(L, "chassis_land_wheel_mule", 88);
  lua_setglobal(L, "e_game_object_type");

  lua_newtable(L);
  const char* iconNames[] = {"icon_chassis_16_wing_small", "icon_chassis_16_wing_large",
      "icon_chassis_16_rotor_small", "icon_chassis_16_rotor_large", "icon_attachment_16_none",
      "icon_chassis_16_wheel_small", "icon_chassis_16_wheel_medium", "icon_chassis_16_wheel_large",
      "icon_chassis_16_wheel_mule", "icon_chassis_wing_small", "icon_chassis_wing_large"};
  for (int i = 0; i < 11; ++i) SetInteger(L, iconNames[i], i + 1);
  lua_setglobal(L, "atlas_icons");
  lua_pushcfunction(L, BeginGetUiRegionIndex);
  lua_setglobal(L, "begin_get_ui_region_index");
  lua_pushcfunction(L, Iff);
  lua_setglobal(L, "iff");

  lua_newtable(L);
  lua_newtable(L);
  lua_pushcfunction(L, VehicleDefinitions);
  lua_setfield(L, -2, "definitions");
  lua_setfield(L, -2, "vehicles");
  lua_newtable(L);
  lua_pushcfunction(L, LegacyAircraftTypes);
  lua_setfield(L, -2, "custom_aircraft_types");
  lua_setfield(L, -2, "experimental");
  lua_setglobal(L, "cc2x");
  lua_pushcfunction(L, StockChassisData);
  lua_setglobal(L, "get_chassis_data_by_definition_index");

  if (luaL_loadfilex(L, argv[1], "t") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
    const char* error = lua_tostring(L, -1);
    std::fprintf(stderr, "selector screen load failed: %s\n", error ? error : "unknown");
    lua_close(L);
    return 4;
  }
  if (luaL_loadfilex(L, argv[2], "t") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
    const char* error = lua_tostring(L, -1);
    std::fprintf(stderr, "vehicle library load failed: %s\n", error ? error : "unknown");
    lua_close(L);
    return 8;
  }
  lua_getglobal(L, "get_is_vehicle_air");
  lua_pushinteger(L, 102);
  if (lua_pcall(L, 1, 1, 0) != LUA_OK || !lua_toboolean(L, -1)) return 9;
  lua_pop(L, 1);
  lua_getglobal(L, "get_is_vehicle_air");
  lua_pushinteger(L, 103);
  if (lua_pcall(L, 1, 1, 0) != LUA_OK || !lua_toboolean(L, -1)) return 10;
  lua_pop(L, 1);
  lua_getglobal(L, "get_chassis_data_by_definition_index");
  lua_pushinteger(L, 102);
  if (lua_pcall(L, 1, 4, 0) != LUA_OK || std::string(lua_tostring(L, -4)) != "F-16" ||
      lua_tointeger(L, -3) != 0x40000001) return 11;
  lua_settop(L, 0);
  lua_getglobal(L, "get_chassis_data_by_definition_index");
  lua_pushinteger(L, 103);
  if (lua_pcall(L, 1, 4, 0) != LUA_OK || std::string(lua_tostring(L, -4)) != "F-15" ||
      lua_tointeger(L, -3) != 0x40000002) return 12;
  lua_settop(L, 0);
  lua_getglobal(L, "get_chassis_data_by_definition_index");
  lua_pushinteger(L, 104);
  if (lua_pcall(L, 1, 4, 0) != LUA_OK ||
      std::string(lua_tostring(L, -4)) != "Third aircraft definition test" ||
      lua_tointeger(L, -3) != 0x40000005) return 15;
  lua_settop(L, 0);
  lua_getglobal(L, "get_chassis_image_by_definition_index");
  lua_pushinteger(L, 102);
  if (lua_pcall(L, 1, 1, 0) != LUA_OK || lua_tointeger(L, -1) != 0x40000003) {
    std::fprintf(stderr, "F-16 loadout diagram did not use its custom full-size atlas region\n");
    lua_close(L);
    return 13;
  }
  lua_settop(L, 0);
  lua_getglobal(L, "get_chassis_image_by_definition_index");
  lua_pushinteger(L, 103);
  if (lua_pcall(L, 1, 1, 0) != LUA_OK || lua_tointeger(L, -1) != 0x40000004) {
    std::fprintf(stderr, "F-15 loadout diagram did not use its custom full-size atlas region\n");
    lua_close(L);
    return 14;
  }
  lua_settop(L, 0);
  lua_getglobal(L, "get_chassis_image_by_definition_index");
  lua_pushinteger(L, 104);
  if (lua_pcall(L, 1, 1, 0) != LUA_OK || lua_tointeger(L, -1) != 0x40000007) {
    std::fprintf(stderr, "dynamic aircraft loadout diagram did not use its registered 64px atlas region\n");
    lua_close(L);
    return 16;
  }
  lua_settop(L, 0);
  lua_getglobal(L, "get_is_vehicle_air");
  lua_pushinteger(L, 104);
  if (lua_pcall(L, 1, 1, 0) != LUA_OK || !lua_toboolean(L, -1)) return 17;
  lua_settop(L, 0);
  lua_pushinteger(L, 8); lua_setglobal(L, "g_selected_bay_index");
  if (!CheckOptions(L, 8, 8)) {
    std::fprintf(stderr, "aircraft selector did not preserve stock choices and append all ready custom definitions\n");
    lua_close(L);
    return 5;
  }
  lua_getglobal(L, "get_chassis_data_by_definition_index");
  lua_pushinteger(L, 102);
  if (lua_pcall(L, 1, 4, 0) != LUA_OK || !lua_isstring(L, -4) ||
      std::string(lua_tostring(L, -4)) != "F-16") {
    std::fprintf(stderr, "custom chassis label not available\n");
    lua_close(L);
    return 6;
  }
  lua_settop(L, 0);
  lua_pushinteger(L, 0); lua_setglobal(L, "g_selected_bay_index");
  if (!CheckOptions(L, 0, 5)) {
    std::fprintf(stderr, "ground selector changed while adding custom aircraft\n");
    lua_close(L);
    return 7;
  }
  lua_close(L);
  std::puts("Custom aircraft selector contract passed");
  return 0;
}
