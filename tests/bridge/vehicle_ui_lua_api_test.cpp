#include "api/ui/resource_registry.h"
#include "api/vehicle/definition_registry.h"
#include "api/vehicle/lua_api.h"
#include "cc2x.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <cstdint>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace {

cc2x_lua_fn g_definitions = nullptr;
cc2x_lua_fn g_weaponIcons = nullptr;
cc2x_lua_fn g_isCustomAircraftType = nullptr;
std::string g_namespace, g_name;
std::vector<std::string> g_features;

lua_State* State(cc2x_lua* L) { return reinterpret_cast<lua_State*>(L); }
int LuaAdd(const char* nameSpace, const char* name, cc2x_lua_fn function) {
  const std::string ns = nameSpace ? nameSpace : "";
  const std::string key = name ? name : "";
  if (ns == "vehicles" && key == "definitions") g_definitions = function;
  else if (ns == "ui" && key == "weapon_icons") g_weaponIcons = function;
  else return 0;
  g_namespace = ns;
  g_name = key;
  return function != nullptr;
}
int LuaAddGlobal(const char* name, cc2x_lua_fn function) {
  if (!name || std::string(name) != "cc2x_is_custom_aircraft_type" || !function) return 0;
  g_isCustomAircraftType = function;
  return 1;
}
void LuaFeature(const char* name) { g_features.emplace_back(name ? name : ""); }
int LuaGetTop(cc2x_lua* L) { return lua_gettop(State(L)); }
int LuaType(cc2x_lua* L, int index) { return lua_type(State(L), index); }
int LuaToInteger(cc2x_lua* L, int index, int64_t* out) {
  int isNumber = 0;
  const auto value = lua_tointegerx(State(L), index, &isNumber);
  if (isNumber && out) *out = static_cast<int64_t>(value);
  return isNumber;
}
void LuaPushNil(cc2x_lua* L) { lua_pushnil(State(L)); }
void LuaPushBoolean(cc2x_lua* L, int value) { lua_pushboolean(State(L), value); }
void LuaPushInteger(cc2x_lua* L, int64_t value) { lua_pushinteger(State(L), value); }
void LuaPushString(cc2x_lua* L, const char* value) { lua_pushstring(State(L), value); }
void LuaNewTable(cc2x_lua* L) { lua_newtable(State(L)); }
void LuaSetField(cc2x_lua* L, const char* key) { lua_setfield(State(L), -2, key); }
void LuaAppend(cc2x_lua* L, int64_t index) { lua_rawseti(State(L), -2, static_cast<lua_Integer>(index)); }

void Ready(cc2::api::vehicle::DefinitionRegistry& registry, std::uint32_t id) {
  if (registry.Mark(id, cc2::api::vehicle::NameRegistered) &&
      registry.Mark(id, cc2::api::vehicle::ArrayInitialized) &&
      registry.Mark(id, cc2::api::vehicle::XmlParsed))
    registry.Mark(id, cc2::api::vehicle::Ready);
}

}  // namespace

int main() {
  using namespace cc2;
  auto& definitions = api::vehicle::Registry();
  host::VehicleDeclaration imageDefinition;
  imageDefinition.owner = L"sample.mod";
  imageDefinition.localId = L"plane";
  imageDefinition.identity = L"sample.mod.plane";
  imageDefinition.name = L"Test plane";
  imageDefinition.resourceName = L"sample_plane";
  imageDefinition.nativeBaseType = 8;
  imageDefinition.ui = L"plane";
  host::VehicleDeclaration noImageDefinition;
  noImageDefinition.owner = L"sample.mod";
  noImageDefinition.localId = L"fallback";
  noImageDefinition.identity = L"sample.mod.fallback";
  noImageDefinition.name = L"Fallback plane";
  noImageDefinition.resourceName = L"sample_fallback";
  noImageDefinition.nativeBaseType = 10;
  host::VehicleDeclaration unreadyDefinition = noImageDefinition;
  unreadyDefinition.localId = L"unready";
  unreadyDefinition.identity = L"sample.mod.unready";
  unreadyDefinition.resourceName = L"sample_unready";
  std::string error;
  if (!definitions.Publish({imageDefinition, noImageDefinition, unreadyDefinition}, 104, &error)) {
    std::cerr << "definition registry setup failed: " << error << '\n';
    return 1;
  }
  Ready(definitions, 104);
  Ready(definitions, 105);

  auto& resources = api::ui::Registry();
  std::vector<api::ui::ResourceDescriptor> images;
  const wchar_t* roles[] = {L"small", L"large", L"loadout"};
  const std::uint32_t tokens[] = {0x40000005u, 0x40000006u, 0x40000007u};
  for (int i = 0; i < 3; ++i) {
    api::ui::ResourceDescriptor image;
    image.owner = L"sample.mod";
    image.logicalId = std::wstring(L"vehicles/plane/") + roles[i];
    image.sourcePath = L"plane.png";
    image.virtualPath = "textures/cc2x_runtime/surface_0000000" + std::to_string(i + 1) + ".txtr";
    image.type = api::ui::ResourceType::TxtrSurface;
    image.generatedPath = L"cache/surface.txtr";
    image.regionName = "cx00000" + std::to_string(i + 1);
    image.regionToken = tokens[i];
    images.push_back(std::move(image));
  }
  const wchar_t* weaponOwners[] = {L"sample.mod", L"other.mod", L"sample.mod", L"sample.mod"};
  const wchar_t* weaponIds[] = {L"missile", L"missile_alt", L"camera", L"pending"};
  const wchar_t* weaponTypes[] = {
      L"attachment_hardpoint_missile_ir", L"attachment_hardpoint_missile_ir",
      L"attachment_camera_plane", L"attachment_hardpoint_torpedo"};
  const std::uint32_t weaponTokens[] = {0x40000008u, 0x40000009u, 0x4000000Au, 0x4000000Bu};
  for (int i = 0; i < 4; ++i) {
    for (int role = 0; role < 2; ++role) {
      api::ui::ResourceDescriptor image;
      image.owner = weaponOwners[i];
      image.logicalId = std::wstring(L"weapons/") + weaponIds[i] +
          (role == 0 ? L"/small" : L"/large");
      image.engineType = weaponTypes[i];
      image.sourcePath = L"weapon.png";
      image.virtualPath = "textures/cc2x_runtime/weapon_" + std::to_string(i) +
          (role == 0 ? "_small" : "_large") + ".txtr";
      image.type = api::ui::ResourceType::TxtrSurface;
      image.generatedPath = L"cache/weapon.txtr";
      image.regionName = "weapon_region_" + std::to_string(i) +
          (role == 0 ? "_small" : "_large");
      image.regionToken = weaponTokens[i] + (role == 0 ? 0x100u : 0u);
      images.push_back(std::move(image));
    }
  }
  if (!resources.Publish(std::move(images), &error)) {
    std::cerr << "resource registry setup failed: " << error << '\n';
    return 2;
  }
  for (const auto* role : roles) {
    const auto logical = std::wstring(L"vehicles/plane/") + role;
    if (!resources.Mark(L"sample.mod", logical, api::ui::EngineRequested) ||
        !resources.Mark(L"sample.mod", logical, api::ui::EngineReady)) return 3;
  }
  for (int i = 0; i < 3; ++i) {
    for (const auto* role : {L"small", L"large"}) {
      const auto logical = std::wstring(L"weapons/") + weaponIds[i] + L"/" + role;
      if (!resources.Mark(weaponOwners[i], logical, api::ui::EngineRequested) ||
          !resources.Mark(weaponOwners[i], logical, api::ui::EngineReady)) return 3;
    }
  }

  cc2x_api apiTable{};
  apiTable.api_version = 8;
  apiTable.size = sizeof(apiTable);
  apiTable.lua_add = LuaAdd;
  apiTable.lua_add_global = LuaAddGlobal;
  apiTable.lua_feature = LuaFeature;
  apiTable.lua_gettop = LuaGetTop;
  apiTable.lua_type = LuaType;
  apiTable.lua_tointeger = LuaToInteger;
  apiTable.lua_pushnil = LuaPushNil;
  apiTable.lua_pushboolean = LuaPushBoolean;
  apiTable.lua_pushinteger = LuaPushInteger;
  apiTable.lua_pushstring = LuaPushString;
  apiTable.lua_newtable = LuaNewTable;
  apiTable.lua_setfield = LuaSetField;
  apiTable.lua_appendi = LuaAppend;
  if (!api::vehicle::RegisterStockLuaApi(&apiTable) ||
      !api::vehicle::RegisterStockLuaCompatibilityApi(&apiTable) ||
      g_namespace != "ui" || g_name != "weapon_icons" ||
      !g_definitions || !g_weaponIcons || !g_isCustomAircraftType ||
      std::find(g_features.begin(), g_features.end(), "vehicle_definitions") == g_features.end() ||
      std::find(g_features.begin(), g_features.end(), "weapon_ui_icons") == g_features.end() ||
      std::find(g_features.begin(), g_features.end(), "vehicle_ui_compatibility") == g_features.end()) {
    std::cerr << "stock Lua API registration mismatch\n";
    return 4;
  }

  lua_State* L = luaL_newstate();
  if (!L) return 5;
  const int results = g_definitions(reinterpret_cast<cc2x_lua*>(L));
  if (results != 1 || !lua_istable(L, -1) || lua_rawlen(L, -1) != 2) {
    std::cerr << "definition query did not return exactly the two ready entries\n";
    lua_close(L);
    return 6;
  }
  lua_rawgeti(L, -1, 1);
  lua_getfield(L, -1, "id");
  const bool fallbackIdMatches = lua_tointeger(L, -1) == 104;
  lua_pop(L, 1);
  lua_getfield(L, -1, "identity");
  const bool fallbackIdentityMatches = std::string(lua_tostring(L, -1)) == "sample.mod.fallback";
  lua_pop(L, 1);
  lua_getfield(L, -1, "icon16");
  const bool fallbackIconsOmitted = lua_isnil(L, -1);
  lua_pop(L, 1);
  lua_pop(L, 1);
  lua_rawgeti(L, -1, 2);
  lua_getfield(L, -1, "id");
  const bool imageIdMatches = lua_tointeger(L, -1) == 105;
  lua_pop(L, 1);
  lua_getfield(L, -1, "identity");
  const bool imageIdentityMatches = std::string(lua_tostring(L, -1)) == "sample.mod.plane";
  lua_pop(L, 1);
  lua_getfield(L, -1, "icon16");
  const bool icon16Matches = lua_tointeger(L, -1) == tokens[0];
  lua_pop(L, 1);
  lua_getfield(L, -1, "icon64");
  const bool icon64Matches = lua_tointeger(L, -1) == tokens[1];
  lua_pop(L, 1);
  lua_getfield(L, -1, "loadout64");
  const bool loadoutMatches = lua_tointeger(L, -1) == tokens[2];
  lua_pop(L, 2);
  lua_close(L);

  if (!fallbackIdMatches || !fallbackIdentityMatches || !fallbackIconsOmitted ||
      !imageIdMatches || !imageIdentityMatches || !icon16Matches || !icon64Matches ||
      !loadoutMatches) {
    std::cerr << "vehicle metadata or ready atlas tokens were not serialized correctly\n";
    return 7;
  }

  L = luaL_newstate();
  if (!L) return 8;
  const int weaponResultCount = g_weaponIcons(reinterpret_cast<cc2x_lua*>(L));
  const bool weaponShape = weaponResultCount == 2 && lua_istable(L, -2) &&
      lua_istable(L, -1) && lua_rawlen(L, -2) == 1 && lua_rawlen(L, -1) == 1;
  bool weaponFields = false;
  bool conflictReported = false;
  if (weaponShape) {
    lua_rawgeti(L, -2, 1);
    lua_getfield(L, -1, "owner");
    const bool ownerMatches = lua_isstring(L, -1) &&
        std::string(lua_tostring(L, -1)) == "sample.mod";
    lua_pop(L, 1);
    lua_getfield(L, -1, "identity");
    const bool weaponIdentityMatches = lua_isstring(L, -1) &&
        std::string(lua_tostring(L, -1)) == "camera";
    lua_pop(L, 1);
    lua_getfield(L, -1, "type");
    const bool weaponTypeMatches = lua_isstring(L, -1) &&
        std::string(lua_tostring(L, -1)) == "attachment_camera_plane";
    lua_pop(L, 1);
    lua_getfield(L, -1, "small");
    const bool weaponSmallTokenMatches = lua_tointeger(L, -1) == weaponTokens[2] + 0x100u;
    lua_pop(L, 1);
    lua_getfield(L, -1, "large");
    const bool weaponTokenMatches = lua_tointeger(L, -1) == weaponTokens[2];
    lua_pop(L, 2);
    lua_rawgeti(L, -1, 1);
    conflictReported = lua_isstring(L, -1) &&
        std::string(lua_tostring(L, -1)) == "attachment_hardpoint_missile_ir";
    lua_pop(L, 1);
    weaponFields = ownerMatches && weaponIdentityMatches && weaponTypeMatches &&
        weaponSmallTokenMatches && weaponTokenMatches;
  }
  lua_close(L);
  if (!weaponShape || !weaponFields || !conflictReported) {
    std::cerr << "weapon icon API did not return ready owner-scoped tokens and ambiguity data\n";
    return 9;
  }

  L = luaL_newstate();
  if (!L) return 10;
  lua_pushinteger(L, 104);
  const int customResultCount = g_isCustomAircraftType(reinterpret_cast<cc2x_lua*>(L));
  const bool customAircraft = customResultCount == 1 && lua_toboolean(L, -1);
  lua_settop(L, 0);
  lua_pushinteger(L, 106);  // published but not yet engine-ready
  const int unreadyResultCount = g_isCustomAircraftType(reinterpret_cast<cc2x_lua*>(L));
  const bool unreadyIsFalse = unreadyResultCount == 1 && !lua_toboolean(L, -1);
  lua_settop(L, 0);
  lua_pushinteger(L, 8);  // vanilla aircraft are not custom definitions
  const int stockResultCount = g_isCustomAircraftType(reinterpret_cast<cc2x_lua*>(L));
  const bool stockIsFalse = stockResultCount == 1 && !lua_toboolean(L, -1);
  lua_settop(L, 0);
  lua_pushstring(L, "104");
  const int invalidResultCount = g_isCustomAircraftType(reinterpret_cast<cc2x_lua*>(L));
  const bool invalidResult = invalidResultCount == 2 && lua_isnil(L, -2) &&
      std::string(lua_tostring(L, -1)) == "expected_definition_id";
  lua_close(L);
  if (!customAircraft || !unreadyIsFalse || !stockIsFalse || !invalidResult) {
    std::cerr << "stock Lua custom-aircraft predicate did not enforce registry readiness and argument contract\n";
    return 9;
  }
  std::cout << "PASS stock Lua vehicle definitions include only ready records and ready UI tokens\n";
  return 0;
}
