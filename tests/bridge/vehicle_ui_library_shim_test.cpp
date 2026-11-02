extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "api/vehicle/vehicle_hud_source_patch.h"

#include <cstdio>
#include <string>

namespace {
bool Run(lua_State* state, const char* source) {
  if (luaL_loadstring(state, source) != LUA_OK || lua_pcall(state, 0, 0, 0) != LUA_OK) {
    const char* error = lua_tostring(state, -1);
    std::fprintf(stderr, "UI library shim test failed: %s\n", error ? error : "unknown");
    return false;
  }
  lua_settop(state, 0);
  return true;
}

bool Load(lua_State* state, const char* path) {
  if (luaL_loadfilex(state, path, "t") != LUA_OK || lua_pcall(state, 0, 0, 0) != LUA_OK) {
    const char* error = lua_tostring(state, -1);
    std::fprintf(stderr, "UI library shim load failed: %s\n", error ? error : "unknown");
    return false;
  }
  lua_settop(state, 0);
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  {
    constexpr char stockHud[] =
        "function update()\r\n"
        "                local def = vehicle:get_definition_index()\r\n"
        "                if def == e_game_object_type.chassis_air_wing_light\r\n"
        "                or def == e_game_object_type.chassis_air_wing_heavy\r\n"
        "                then render_flight_hud() end\r\n"
        "end\r\n";
    std::string patchedHud;
    const auto result = cc2::api::vehicle::PatchVehicleHudSource(stockHud, &patchedHud);
    if (result != cc2::api::vehicle::VehicleHudPatchResult::kPatched ||
        patchedHud.find("if (type(cc2x_is_custom_aircraft_type) == \"function\" and cc2x_is_custom_aircraft_type(def))\r\n") == std::string::npos ||
        patchedHud.find("                or def == e_game_object_type.chassis_air_wing_light\r\n") == std::string::npos ||
        std::string(stockHud).find("cc2x_is_custom_aircraft_type") != std::string::npos) {
      std::fprintf(stderr, "vehicle_hud source compatibility patch did not preserve the stock condition\n");
      return 6;
    }
    std::string idempotentOutput;
    if (cc2::api::vehicle::PatchVehicleHudSource(patchedHud, &idempotentOutput) !=
        cc2::api::vehicle::VehicleHudPatchResult::kAlreadyPatched) {
      std::fprintf(stderr, "vehicle_hud source compatibility patch is not idempotent\n");
      return 7;
    }
    if (cc2::api::vehicle::PatchVehicleHudSource("unrelated lua", &patchedHud) !=
        cc2::api::vehicle::VehicleHudPatchResult::kNoMatch) {
      std::fprintf(stderr, "unrelated Lua source matched the vehicle_hud patch\n");
      return 8;
    }
    constexpr char duplicateHud[] =
        "if def == e_game_object_type.chassis_air_wing_light\n"
        "if def == e_game_object_type.chassis_air_wing_light\n";
    if (cc2::api::vehicle::PatchVehicleHudSource(duplicateHud, &patchedHud) !=
        cc2::api::vehicle::VehicleHudPatchResult::kAmbiguous) {
      std::fprintf(stderr, "ambiguous vehicle_hud source was not rejected\n");
      return 9;
    }
  }
  lua_State* state = luaL_newstate();
  if (!state) return 3;
  luaL_openlibs(state);

  const char* stock = R"lua(
e_game_object_type = {
  count = 102,
  chassis_air_wing_light = 8,
  chassis_air_wing_heavy = 10,
  chassis_air_rotor_light = 12,
  chassis_air_rotor_heavy = 14,
  attachment_hardpoint_missile_ir = 42,
}
atlas_icons = {
  icon_chassis_16_wing_small = 8,
  icon_chassis_16_wing_large = 10,
  icon_chassis_16_rotor_small = 12,
  icon_chassis_16_rotor_large = 14,
}
cc2x = { vehicles = { definitions = function()
  return {
    { id=102, name="Alpha", base_type=8, aircraft=true, ready=true,
      icon16=0x40000001, loadout64=0x40000011 },
    { id=103, name="Beta", base_type=10, aircraft=true, ready=true,
      icon16=0x40000002, loadout64=0x40000012 },
    { id=104, name="Third", base_type=8, aircraft=true, ready=true,
      icon16=0x40000003, loadout64=0x40000013 },
    { id=105, name="Not ready", base_type=8, aircraft=true, ready=false,
      icon16=0x40000004, loadout64=0x40000014 },
    { id=107, name="Rotor", base_type=12, aircraft=true, ready=true,
      icon16=0x40000005, loadout64=0x40000015 },
    { id=109, name="Rotor fallback", base_type=12, aircraft=true, ready=true },
  }
end }, ui = { weapon_icons = function()
  return {
    { owner="sample.weapons", identity="test_missile",
      type="attachment_hardpoint_missile_ir",
      small=0x40000021, large=0x40000020 },
  }, {}
end } }
assert(cc2 == nil, "test must mirror the stock game's cc2x namespace")
cc2x_custom_aircraft_predicate_calls = 0
function cc2x_is_custom_aircraft_type(id)
  cc2x_custom_aircraft_predicate_calls = cc2x_custom_aircraft_predicate_calls + 1
  return id == 102 or id == 103 or id == 104 or id == 107 or id == 109
end

function get_chassis_data_by_definition_index(index)
  return "stock-" .. tostring(index), 700 + index, "abbr-" .. tostring(index), "stock description"
end
function get_chassis_image_by_definition_index(index) return 900 + index end
function get_is_vehicle_air(index) return index == 8 or index == 10 or index == 12 or index == 14 end
function get_is_vehicle_type_waypoint_capable(index)
  return index == 2 or index == 8 or index == 10 or index == 12 or index == 14
end
function get_attachment_icons(index) return 600 + index, 700 + index end
function get_selected_vehicle_attachment_options(_)
  return { { type=42, region=742 }, { type=43, region=743 } }
end
function get_selected_chassis_options(_)
  if g_selected_bay_index >= 8 then return {
    { type=-1, region=1 }, { type=8, region=8 }, { type=10, region=10 },
    { type=12, region=12 }, { type=14, region=14 },
  } end
  return { { type=-1, region=1 }, { type=2, region=2 }, { type=4, region=4 } }
end
function get_ui_vehicle_chassis_attachments(vehicle)
  return vehicle:get_definition_index(), vehicle:get_attachment_count()
end
e_vehicle_dock_state = { dock_queue=1, docking=2, docking_taxi=3, other=0 }
local function make_vehicle(definition, id, dock_state, parent_id)
  local vehicle = { definition=definition, id=id, dock_state=dock_state, parent_id=parent_id }
  function vehicle:get() return true end
  function vehicle:get_id() return self.id end
  function vehicle:get_definition_index() return self.definition end
  function vehicle:get_dock_state() return self.dock_state end
  function vehicle:get_dock_parent_id() return self.parent_id end
  return vehicle
end
local stock_docked_vehicle = make_vehicle(8, 80, e_vehicle_dock_state.docking, 77)
map_vehicles = {
  make_vehicle(102, 200, e_vehicle_dock_state.docking, 77),
  make_vehicle(103, 201, e_vehicle_dock_state.docking, 78),
  make_vehicle(104, 202, e_vehicle_dock_state.docking_taxi, 77),
  make_vehicle(105, 203, e_vehicle_dock_state.docking, 77),
  make_vehicle(2, 204, e_vehicle_dock_state.docking, 77),
  make_vehicle(107, 205, e_vehicle_dock_state.dock_queue, 77),
  make_vehicle(102, 206, e_vehicle_dock_state.other, 77),
  make_vehicle(109, 207, e_vehicle_dock_state.docking, 77),
}
function update_get_map_vehicle_count() return #map_vehicles end
function update_get_map_vehicle_by_index(index) return map_vehicles[index + 1] end
function get_docking_vehicles(parent)
  return { { vehicle=stock_docked_vehicle, is_wing=true, is_rotor=false } }
end
)lua";
  if (!Run(state, stock) || !Load(state, argv[1])) {
    lua_close(state);
    return 4;
  }

  const char* assertions = R"lua(
 g_selected_bay_index = 8
local options = get_selected_chassis_options(8)
assert(#options == 10, "stock aircraft options were not preserved and extended: got " .. #options)
local ids = { -1, 8, 10, 12, 14, 102, 103, 104, 107, 109 }
for i, id in ipairs(ids) do assert(options[i].type == id, "unexpected aircraft option order") end
assert(options[6].region == 0x40000001 and options[7].region == 0x40000002 and
       options[8].region == 0x40000003 and options[9].region == 0x40000005 and
       options[10].region == 12,
       "custom selector icons were not assigned")
g_selected_bay_index = 0
assert(#get_selected_chassis_options(8) == 3,
       "ground selector must follow the stock global even when its unused argument differs")
g_selected_bay_index = 8

local name, icon, abbrev, description = get_chassis_data_by_definition_index(103)
assert(name == "Beta" and icon == 0x40000002 and abbrev == "abbr-10" and
       description == "stock description", "custom compact vehicle UI data is wrong")
assert(get_chassis_data_by_definition_index(8) == "stock-8", "stock label fallback changed")
assert(get_chassis_image_by_definition_index(104) == 0x40000013,
       "custom loadout diagram token is wrong")
assert(get_chassis_image_by_definition_index(8) == 908, "stock large-icon fallback changed")
local rotorName, rotorIcon = get_chassis_data_by_definition_index(109)
assert(rotorName == "Rotor fallback" and rotorIcon == 12,
       "missing custom rotor icon did not fall back to its stock-class icon")
assert(get_is_vehicle_air(104) and not get_is_vehicle_air(99), "air classification is wrong")
assert(get_is_vehicle_type_waypoint_capable(102) and
       get_is_vehicle_type_waypoint_capable(103) and
       not get_is_vehicle_type_waypoint_capable(99) and
       get_is_vehicle_type_waypoint_capable(2),
       "waypoint-capability lookup did not inherit the stock class only for custom aircraft")
local weapon_large, weapon_small = get_attachment_icons(42)
assert(weapon_large == 0x40000020 and weapon_small == 0x40000021,
       "custom large and compact weapon icons were not both resolved")
local stock_weapon_large, stock_weapon_small = get_attachment_icons(43)
assert(stock_weapon_large == 643 and stock_weapon_small == 743,
       "unmapped stock weapon icon behavior changed")
local weapon_options = get_selected_vehicle_attachment_options(0)
assert(weapon_options[1].type == 42 and weapon_options[1].region == 0x40000021 and
       weapon_options[2].type == 43 and weapon_options[2].region == 743,
       "compact icon was not substituted only in the matching loadout selector row")
assert(cc2x_is_custom_aircraft_type(102) and cc2x_is_custom_aircraft_type(107) and
       not cc2x_is_custom_aircraft_type(8) and not cc2x_is_custom_aircraft_type(105),
       "generic HUD custom-aircraft classification is wrong")
assert(cc2x_custom_aircraft_predicate_calls > 0,
       "UI shim did not delegate custom-type classification to the core predicate")

local vehicle = {}
function vehicle:get_definition_index() return 103 end
function vehicle:get_attachment_count() return 4 end
local layout_type, attachment_count = get_ui_vehicle_chassis_attachments(vehicle)
assert(layout_type == 10 and attachment_count == 4,
       "stock attachment layout did not receive the definition's base class")

local parent = { get_id = function() return 77 end }
local docked = get_docking_vehicles(parent)
assert(#docked == 5, "vanilla docking vehicle and four eligible custom aircraft should be present")
assert(docked[1].vehicle:get_id() == 80 and docked[1].is_wing,
       "vanilla docking result was not preserved")
assert(docked[2].vehicle:get_id() == 200 and docked[2].is_wing and not docked[2].is_rotor,
       "custom wing in docking state was not classified as wing")
assert(docked[3].vehicle:get_id() == 202 and docked[3].is_wing,
       "custom wing taxi state was not included")
assert(docked[4].vehicle:get_id() == 205 and not docked[4].is_wing and docked[4].is_rotor,
       "custom rotor queue state was not classified as rotor")
assert(docked[5].vehicle:get_id() == 207 and not docked[5].is_wing and docked[5].is_rotor,
       "custom rotor docking state was not classified as rotor")
)lua";
  const bool passed = Run(state, assertions);
  lua_close(state);
  if (!passed) return 5;
  std::puts("Generic stock-library UI extension contract passed");
  return 0;
}
