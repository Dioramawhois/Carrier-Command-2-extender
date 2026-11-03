#include "lua_api.h"

#include "cc2x.h"
#include "api/ui/resource_registry.h"
#include "api/ui/weapon_catalog.h"
#include "api/vehicle/definition_registry.h"
#include "api/vehicle/ui_catalog.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace cc2::api::vehicle {
namespace {

const cc2x_api* g_api = nullptr;

std::string Utf8(const std::wstring& value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string result(static_cast<std::size_t>(size), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), result.data(), size, nullptr, nullptr) != size)
    return {};
  return result;
}

void SetInteger(cc2x_lua* L, const char* key, std::uint32_t value) {
  g_api->lua_pushinteger(L, value);
  g_api->lua_setfield(L, key);
}

int VehicleDefinitions(cc2x_lua* L) {
  if (!g_api || g_api->lua_gettop(L) != 0) {
    if (g_api && g_api->lua_pushnil && g_api->lua_pushstring) {
      g_api->lua_pushnil(L);
      g_api->lua_pushstring(L, "no_arguments_expected");
      return 2;
    }
    return 0;
  }

  const auto& registry = Registry();
  g_api->lua_newtable(L);
  std::int64_t output = 1;
  const auto& uiRegistry = ui::Registry();
  for (std::uint32_t id = registry.FirstId(); id < registry.Limit(); ++id) {
    const auto* record = registry.Find(id);
    if (!record || !registry.ReadyFor(id)) continue;

    const auto identity = Utf8(record->descriptor.identity);
    const auto uiIdentity = Utf8(record->descriptor.ui);
    if (identity.empty()) continue;
    const auto icons = ResolveUiRegionTokens(record->descriptor, uiRegistry);

    g_api->lua_newtable(L);
    SetInteger(L, "id", id);
    SetInteger(L, "base_type", record->descriptor.nativeBaseType);
    g_api->lua_pushstring(L, identity.c_str()); g_api->lua_setfield(L, "identity");
    g_api->lua_pushstring(L, record->displayName.c_str()); g_api->lua_setfield(L, "name");
    g_api->lua_pushboolean(L, 1); g_api->lua_setfield(L, "aircraft");
    g_api->lua_pushboolean(L, 1); g_api->lua_setfield(L, "ready");
    if (!uiIdentity.empty()) {
      g_api->lua_pushstring(L, uiIdentity.c_str()); g_api->lua_setfield(L, "ui");
    }
    if (icons.icon16) SetInteger(L, "icon16", icons.icon16);
    if (icons.icon64) SetInteger(L, "icon64", icons.icon64);
    if (icons.loadout64) SetInteger(L, "loadout64", icons.loadout64);
    g_api->lua_appendi(L, output++);
  }
  return 1;
}

int WeaponIconBindings(cc2x_lua* L) {
  if (!g_api || g_api->lua_gettop(L) != 0) {
    if (g_api && g_api->lua_pushnil && g_api->lua_pushstring) {
      g_api->lua_pushnil(L);
      g_api->lua_pushstring(L, "no_arguments_expected");
      return 2;
    }
    return 0;
  }

  std::vector<std::wstring> ambiguous;
  const auto bindings = ui::ResolveWeaponIconTokens(ui::Registry(), &ambiguous);
  g_api->lua_newtable(L);
  std::int64_t output = 1;
  for (const auto& binding : bindings) {
    const auto owner = Utf8(binding.owner);
    const auto identity = Utf8(binding.identity);
    const auto engineType = Utf8(binding.engineType);
    if (owner.empty() || identity.empty() || engineType.empty()) continue;
    g_api->lua_newtable(L);
    g_api->lua_pushstring(L, owner.c_str()); g_api->lua_setfield(L, "owner");
    g_api->lua_pushstring(L, identity.c_str()); g_api->lua_setfield(L, "identity");
    g_api->lua_pushstring(L, engineType.c_str()); g_api->lua_setfield(L, "type");
    if (binding.smallRegionToken) SetInteger(L, "small", binding.smallRegionToken);
    SetInteger(L, "large", binding.largeRegionToken);
    g_api->lua_appendi(L, output++);
  }

  g_api->lua_newtable(L);
  output = 1;
  for (const auto& type : ambiguous) {
    const auto engineType = Utf8(type);
    if (engineType.empty()) continue;
    g_api->lua_pushstring(L, engineType.c_str());
    g_api->lua_appendi(L, output++);
  }
  return 2;
}

int IsCustomAircraftType(cc2x_lua* L) {
  if (!g_api || !g_api->lua_gettop || !g_api->lua_type || !g_api->lua_tointeger ||
      !g_api->lua_pushnil || !g_api->lua_pushstring || !g_api->lua_pushboolean)
    return 0;
  if (g_api->lua_gettop(L) != 1) {
    g_api->lua_pushnil(L);
    g_api->lua_pushstring(L, "expected_definition_id");
    return 2;
  }

  std::int64_t value = 0;
  if (g_api->lua_type(L, 1) != CC2X_LUA_NUMBER ||
      !g_api->lua_tointeger(L, 1, &value) || value < 0 ||
      static_cast<std::uint64_t>(value) > std::numeric_limits<std::uint32_t>::max()) {
    g_api->lua_pushnil(L);
    g_api->lua_pushstring(L, "expected_definition_id");
    return 2;
  }

  const auto id = static_cast<std::uint32_t>(value);
  const auto* record = Registry().Find(id);
  const bool isCustomAircraft = record && Registry().ReadyFor(id) &&
      (record->descriptor.nativeBaseType == 8 || record->descriptor.nativeBaseType == 10);
  g_api->lua_pushboolean(L, isCustomAircraft ? 1 : 0);
  return 1;
}

}  // namespace

bool RegisterStockLuaApi(const cc2x_api* api) {
  if (!api || api->api_version < 5 || !api->lua_add || !api->lua_feature ||
      !api->lua_gettop || !api->lua_newtable || !api->lua_pushinteger ||
      !api->lua_pushboolean || !api->lua_pushstring || !api->lua_setfield ||
      !api->lua_appendi || !api->lua_pushnil) return false;
  g_api = api;
  if (!api->lua_add("vehicles", "definitions", VehicleDefinitions)) {
    g_api = nullptr;
    return false;
  }
  if (!api->lua_add("ui", "weapon_icons", WeaponIconBindings)) {
    g_api = nullptr;
    return false;
  }
  api->lua_feature("vehicle_definitions");
  api->lua_feature("weapon_ui_icons");
  return true;
}

bool RegisterStockLuaCompatibilityApi(const cc2x_api* api) {
  if (!api || api->api_version < 8 ||
      api->size < offsetof(cc2x_api, lua_add_global) + sizeof(api->lua_add_global) ||
      !api->lua_add_global || !api->lua_gettop || !api->lua_type || !api->lua_tointeger ||
      !api->lua_pushnil || !api->lua_pushstring || !api->lua_pushboolean)
    return false;
  g_api = api;
  if (!api->lua_add_global("cc2x_is_custom_aircraft_type", IsCustomAircraftType))
    return false;
  if (api->lua_feature) api->lua_feature("vehicle_ui_compatibility");
  return true;
}

}  // namespace cc2::api::vehicle
