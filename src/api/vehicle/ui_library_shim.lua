-- Core-owned compatibility layer for dynamic vehicle UI data.
-- Injected after the stock library and screen Lua chunks have executed, before
-- the screen's begin() callback. It only wraps helpers that exist in that
-- context and always preserves their stock result for non-CC2X definitions.

if rawget(_G, "__cc2x_vehicle_ui_shim_installed") == true then return end

local api = rawget(_G, "cc2x")
if type(api) ~= "table" or type(api.vehicles) ~= "table" or
    type(api.vehicles.definitions) ~= "function" then
    return
end

local function vehicle_definitions()
    local ok, definitions = pcall(api.vehicles.definitions)
    if not ok or type(definitions) ~= "table" then return {} end
    return definitions
end

local function vehicle_by_id(id)
    if type(id) ~= "number" then return nil end
    for _, definition in ipairs(vehicle_definitions()) do
        if type(definition) == "table" and definition.id == id and
            definition.ready ~= false then
            return definition
        end
    end
    return nil
end

local core_is_custom_aircraft_type = rawget(_G, "cc2x_is_custom_aircraft_type")
local function is_custom_aircraft(definition)
    if type(definition) ~= "table" or definition.aircraft ~= true or
        definition.ready == false or type(definition.id) ~= "number" or
        type(core_is_custom_aircraft_type) ~= "function" then
        return false
    end
    local ok, result = pcall(core_is_custom_aircraft_type, definition.id)
    return ok and result == true
end

local function aircraft_ui_class(definition)
    if not is_custom_aircraft(definition) or type(e_game_object_type) ~= "table" then
        return false, false
    end
    local base_type = definition.base_type
    if base_type == e_game_object_type.chassis_air_wing_light or
        base_type == e_game_object_type.chassis_air_wing_heavy then
        return true, false
    elseif base_type == e_game_object_type.chassis_air_rotor_light or
        base_type == e_game_object_type.chassis_air_rotor_heavy then
        return false, true
    end
    return false, false
end

local function small_icon(definition)
    if type(definition.icon16) == "number" then return definition.icon16 end
    if type(atlas_icons) ~= "table" or type(e_game_object_type) ~= "table" then
        return nil
    end
    local base_type = definition.base_type
    if base_type == e_game_object_type.chassis_air_wing_light then
        return atlas_icons.icon_chassis_16_wing_small
    elseif base_type == e_game_object_type.chassis_air_wing_heavy then
        return atlas_icons.icon_chassis_16_wing_large
    elseif base_type == e_game_object_type.chassis_air_rotor_light then
        return atlas_icons.icon_chassis_16_rotor_small
    elseif base_type == e_game_object_type.chassis_air_rotor_heavy then
        return atlas_icons.icon_chassis_16_rotor_large
    end
    return nil
end

local stock_get_chassis_data = get_chassis_data_by_definition_index
if type(stock_get_chassis_data) == "function" then
    function get_chassis_data_by_definition_index(index, ...)
        local definition = vehicle_by_id(index)
        if definition and definition.aircraft == true then
            local _, stock_icon, abbreviation, description =
                stock_get_chassis_data(definition.base_type, ...)
            return definition.name, small_icon(definition) or stock_icon,
                abbreviation, definition.description or description
        end
        return stock_get_chassis_data(index, ...)
    end
end

local stock_get_chassis_image = get_chassis_image_by_definition_index
if type(stock_get_chassis_image) == "function" then
    function get_chassis_image_by_definition_index(index, ...)
        local definition = vehicle_by_id(index)
        if definition and definition.aircraft == true then
            if type(definition.loadout64) == "number" then return definition.loadout64 end
            return stock_get_chassis_image(definition.base_type, ...)
        end
        return stock_get_chassis_image(index, ...)
    end
end

local stock_is_vehicle_air = get_is_vehicle_air
if type(stock_is_vehicle_air) == "function" then
    function get_is_vehicle_air(index, ...)
        local definition = vehicle_by_id(index)
        if definition and definition.aircraft == true then return true end
        return stock_is_vehicle_air(index, ...)
    end
end

-- The stock waypoint UI classifies capability by the definition ID. Dynamic
-- aircraft inherit this behavior from their registered native base class.
local stock_is_waypoint_capable = get_is_vehicle_type_waypoint_capable
if type(stock_is_waypoint_capable) == "function" then
    function get_is_vehicle_type_waypoint_capable(index, ...)
        local definition = vehicle_by_id(index)
        if definition and definition.aircraft == true then
            return stock_is_waypoint_capable(definition.base_type, ...)
        end
        return stock_is_waypoint_capable(index, ...)
    end
end

-- Weapon/equipment icons share the framework atlas but bind by the stock
-- symbolic attachment type. Large and compact regions are resolved together;
-- stock values remain the fallback for unmapped or incomplete declarations.
local stock_get_attachment_icons = get_attachment_icons
if type(stock_get_attachment_icons) == "function" and
    type(api.ui) == "table" and type(api.ui.weapon_icons) == "function" and
    type(e_game_object_type) == "table" then
    local custom_weapon_icons = {}
    local ok, bindings = pcall(api.ui.weapon_icons)
    if ok and type(bindings) == "table" then
        for _, binding in ipairs(bindings) do
            if type(binding) == "table" and type(binding.type) == "string" and
                binding.type:sub(1, 11) == "attachment_" and
                type(binding.large) == "number" then
                local definition_index = e_game_object_type[binding.type]
                if type(definition_index) == "number" and
                    custom_weapon_icons[definition_index] == nil then
                    custom_weapon_icons[definition_index] = {
                        large = binding.large,
                        small = binding.small,
                    }
                end
            end
        end
    end

    function get_attachment_icons(definition_index, ...)
        local stock_icon, stock_small_icon =
            stock_get_attachment_icons(definition_index, ...)
        local custom_icons = custom_weapon_icons[definition_index]
        if type(custom_icons) == "table" then
            return custom_icons.large or stock_icon,
                custom_icons.small or stock_small_icon
        end
        return stock_icon, stock_small_icon
    end

    -- The loadout attachment picker reads attachment_data.icon16 directly
    -- instead of using get_attachment_icons. Remap only returned option rows
    -- so the same compact token appears in the selector.
    local stock_get_attachment_options = get_selected_vehicle_attachment_options
    if type(stock_get_attachment_options) == "function" then
        function get_selected_vehicle_attachment_options(attachment_type, ...)
            local options = stock_get_attachment_options(attachment_type, ...)
            if type(options) ~= "table" then return options end
            for _, option in ipairs(options) do
                if type(option) == "table" and type(option.type) == "number" then
                    local custom_icons = custom_weapon_icons[option.type]
                    if type(custom_icons) == "table" and
                        type(custom_icons.small) == "number" then
                        option.region = custom_icons.small
                    end
                end
            end
            return options
        end
    end
end

local stock_get_selected_options = get_selected_chassis_options
if type(stock_get_selected_options) == "function" then
    function get_selected_chassis_options(bay_index, ...)
        local options = stock_get_selected_options(bay_index, ...)
        if type(options) ~= "table" then return options end
        -- The current stock implementation ignores its bay_index argument and
        -- branches on this global. Mirror that source of truth so the extension
        -- cannot append aircraft to the ground option list on a mismatched call.
        local selected_bay = g_selected_bay_index
        if type(selected_bay) ~= "number" or selected_bay < 8 then return options end

        local seen = {}
        for _, option in ipairs(options) do
            if type(option) == "table" then seen[option.type] = true end
        end
        for _, definition in ipairs(vehicle_definitions()) do
            if is_custom_aircraft(definition) and not seen[definition.id] then
                local icon = small_icon(definition)
                if icon ~= nil then
                    options[#options + 1] = { region = icon, type = definition.id }
                    seen[definition.id] = true
                end
            end
        end
        return options
    end
end

local stock_get_attachments = get_ui_vehicle_chassis_attachments
if type(stock_get_attachments) == "function" then
    function get_ui_vehicle_chassis_attachments(vehicle, ...)
        local vehicle_type = type(vehicle)
        if (vehicle_type ~= "table" and vehicle_type ~= "userdata") or
            type(vehicle.get_definition_index) ~= "function" then
            return stock_get_attachments(vehicle, ...)
        end
        local definition = vehicle_by_id(vehicle:get_definition_index())
        if not definition or type(definition.base_type) ~= "number" then
            return stock_get_attachments(vehicle, ...)
        end

        local compatibility_view = setmetatable({}, {
            __index = function(_, key)
                if key == "get_definition_index" then
                    return function() return definition.base_type end
                end
                local member = vehicle[key]
                if type(member) == "function" then
                    return function(_, ...) return member(vehicle, ...) end
                end
                return member
            end
        })
        return stock_get_attachments(compatibility_view, ...)
    end
end

local stock_get_docking_vehicles = get_docking_vehicles
if type(stock_get_docking_vehicles) == "function" then
    function get_docking_vehicles(vehicle_parent, ...)
        local docking_vehicles = stock_get_docking_vehicles(vehicle_parent, ...)
        if type(docking_vehicles) ~= "table" or
            type(vehicle_parent) ~= "table" and type(vehicle_parent) ~= "userdata" or
            type(vehicle_parent.get_id) ~= "function" or
            type(update_get_map_vehicle_count) ~= "function" or
            type(update_get_map_vehicle_by_index) ~= "function" or
            type(e_vehicle_dock_state) ~= "table" then
            return docking_vehicles
        end

        local seen = {}
        for _, entry in pairs(docking_vehicles) do
            if type(entry) == "table" and entry.vehicle ~= nil and
                type(entry.vehicle.get_id) == "function" then
                seen[entry.vehicle:get_id()] = true
            end
        end

        local parent_id = vehicle_parent:get_id()
        local vehicle_count = update_get_map_vehicle_count()
        if type(vehicle_count) ~= "number" or vehicle_count < 0 then
            return docking_vehicles
        end
        for index = 0, vehicle_count - 1 do
            local vehicle = update_get_map_vehicle_by_index(index)
            if vehicle ~= nil and type(vehicle.get) == "function" and vehicle:get() and
                type(vehicle.get_id) == "function" and
                type(vehicle.get_definition_index) == "function" and
                type(vehicle.get_dock_state) == "function" and
                type(vehicle.get_dock_parent_id) == "function" then
                local vehicle_id = vehicle:get_id()
                local dock_state = vehicle:get_dock_state()
                local is_docking = dock_state == e_vehicle_dock_state.docking or
                    dock_state == e_vehicle_dock_state.dock_queue or
                    dock_state == e_vehicle_dock_state.docking_taxi
                if not seen[vehicle_id] and is_docking and
                    vehicle:get_dock_parent_id() == parent_id then
                    local definition = vehicle_by_id(vehicle:get_definition_index())
                    local is_wing, is_rotor = aircraft_ui_class(definition)
                    if is_wing or is_rotor then
                        docking_vehicles[#docking_vehicles + 1] = {
                            vehicle = vehicle,
                            is_wing = is_wing,
                            is_rotor = is_rotor,
                        }
                        seen[vehicle_id] = true
                    end
                end
            end
        end
        return docking_vehicles
    end
end

rawset(_G, "__cc2x_vehicle_ui_shim_installed", true)
