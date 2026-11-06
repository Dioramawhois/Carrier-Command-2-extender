-- Carrier Command 2 Core Bridge Lua SDK.
--
-- Automatically installed as cc2 and require('cc2') in each independent VM.
-- Stock update_* closures are not imported from game-owned Lua states.

local M = {
    version = "1.1.0-cc2x-api2-dev",
    lua_api_version = rawget(_G, "cc2_bridge_lua_api_version") or 1,
    game_api = {},
    bridge = {},
    carrier = {},
    animation = {},
    entities = {},
    session = {},
    orders = {},
    settings = {},
    equipment = {},
    tiles = {},
    vehicles = {},
    messages = {},
    events = {},
    module_id = rawget(_G, "cc2_bridge_module_id"),
    native = rawget(_G, "cc2_bridge_native") or {},
    input = { key = { up = 0x26, right = 0x27 } },
    world = rawget(_G, "cc2_bridge_world") or {},
}

-- Screen/UI scripts use cc2x.modules.send(owner, topic, subject, value).
-- Optional additive interfaces. Older runtimes return unavailable; API 2 and
-- all pre-existing selectors keep their contracts.
function M.carrier.motion_available()
    local native = rawget(_G, 'cc2_bridge_motion_available')
    return type(native) == 'function' and native() == true
end
function M.carrier.motion_state(id)
    local native = rawget(_G, 'cc2_bridge_motion_state')
    if type(native) ~= 'function' then return nil, 'unavailable' end
    return native(id)
end
function M.carrier.set_motion(id, active, power)
    local native = rawget(_G, 'cc2_bridge_motion_set')
    if type(native) ~= 'function' then return false, 'unavailable' end
    return native(id, active, power)
end
function M.carrier.contacts(id)
    local native = rawget(_G, 'cc2_bridge_motion_contacts')
    if type(native) ~= 'function' then return nil, 'unavailable' end
    return native(id)
end
function M.tiles.sites(tile_id, kind, definition)
    local native = rawget(_G, "cc2_bridge_tile_sites")
    if type(native) ~= "function" then return nil, "unavailable" end
    return native(tile_id, kind, definition)
end
function M.equipment.available()
    local native = rawget(_G, "cc2_bridge_equipment_available")
    return type(native) == "function" and native() == true
end
function M.equipment.set_attachment(vehicle_id, slot, definition, fill)
    local native = rawget(_G, "cc2_bridge_equipment_set")
    if type(native) ~= "function" then return false, "unavailable" end
    if fill == nil then fill = true end
    return native(vehicle_id, slot, definition, fill)
end
function M.carrier.dock_available()
    local native = rawget(_G, "cc2_bridge_carrier_available")
    return type(native) == "function" and native("dock") == true
end
function M.carrier.recovery_available()
    local native = rawget(_G, "cc2_bridge_carrier_available")
    return type(native) == "function" and native("recovery") == true
end
function M.carrier.dock_request(carrier_id, vehicle_id)
    local native = rawget(_G, "cc2_bridge_carrier_dock_request")
    if type(native) ~= "function" then return false, "unavailable" end
    return native(carrier_id, vehicle_id)
end
function M.carrier.dock_result(request_id)
    local native = rawget(_G, "cc2_bridge_carrier_dock_result")
    if type(native) ~= "function" then return nil, "unavailable" end
    return native(request_id)
end
function M.carrier.recovery_arm(carrier_id, vehicle_id, options)
    local native = rawget(_G, "cc2_bridge_carrier_recovery_arm")
    if type(native) ~= "function" then return false, "unavailable" end
    return native(carrier_id, vehicle_id, options or {})
end
function M.carrier.recovery_disarm(vehicle_id)
    local native = rawget(_G, "cc2_bridge_carrier_recovery_disarm")
    if type(native) ~= "function" then return false, "unavailable" end
    return native(vehicle_id)
end
function M.carrier.recovery_status(vehicle_id)
    local native = rawget(_G, "cc2_bridge_carrier_recovery_status")
    if type(native) ~= "function" then return nil, "unavailable" end
    return native(vehicle_id)
end
function M.settings.get(key, default, minimum, maximum)
    local native = rawget(_G, "cc2_bridge_settings_get")
    if type(native) ~= "function" then return default, "unavailable" end
    if type(default) == "boolean" and minimum == nil and maximum == nil then
        return native(key, default)
    end
    return native(key, default, minimum, maximum)
end
function M.settings.set(key, value)
    local native = rawget(_G, "cc2_bridge_settings_set")
    if type(native) ~= "function" then return false, "unavailable" end
    return native(key, value)
end
function M.settings.reload()
    local native = rawget(_G, "cc2_bridge_settings_reload")
    if type(native) ~= "function" then return false, "unavailable" end
    return native()
end

-- Only this module may drain its queue or publish its reply/state. Requests
-- are not trusted orders: each mod checks its own permissions and arguments.
function M.messages.poll()
    local native = rawget(_G, "cc2_bridge_messages_poll")
    if type(native) ~= "function" then return nil, "unavailable" end
    return native()
end
function M.session.generation()
    local native = rawget(_G, "cc2_bridge_session_generation")
    return type(native) == "function" and native() or 0
end
function M.session.tick()
    local native = rawget(_G, "cc2_bridge_session_tick")
    return type(native) == "function" and native() or 0
end
-- The current engine waypoint receiver, not a general RPC or permission grant.
-- Generation is the core's local epoch, distinct from cc2.session.generation().
function M.orders.available()
    local native = rawget(_G, "cc2_bridge_orders_available")
    return type(native) == "function" and native() == true
end
function M.orders.current()
    local native = rawget(_G, "cc2_bridge_orders_current")
    if type(native) ~= "function" then return nil, "unavailable" end
    return native()
end
function M.session.post(callback)
    local native = rawget(_G, "cc2_bridge_session_post")
    if type(callback) ~= "function" then return nil, "expected_function" end
    if type(native) ~= "function" then return nil, "unavailable" end
    return native(callback)
end
function M.session.cancel(task)
    local native = rawget(_G, "cc2_bridge_session_cancel")
    if type(native) ~= "function" then return false, "unavailable" end
    return native(task)
end
function M.messages.send(owner, topic, subject, value)
    if type(owner) ~= "string" or #owner < 1 or #owner > 128 or owner:find("%z") then
        return false, "invalid_recipient"
    end
    local native = rawget(_G, "cc2_bridge_messages_send")
    if type(native) ~= "function" then return false, "unavailable" end
    return native(owner, topic, subject, value)
end
function M.messages.read(owner, topic, subject)
    local native = rawget(_G, "cc2_bridge_messages_read")
    if type(native) ~= "function" then return nil, "unavailable" end
    return native(owner, topic, subject)
end
function M.messages.publish(topic, subject, value)
    if type(topic) ~= "string" or #topic < 1 or #topic > 64 or
        not topic:match("^[%w_.-]+$") or type(subject) ~= "number" or
        subject % 1 ~= 0 or subject < 0 or subject > 4294967295 or
        type(value) ~= "number" or value ~= value or math.abs(value) == math.huge then
        return false, "invalid_message"
    end
    local native = rawget(_G, "cc2_bridge_messages_publish")
    if type(native) ~= "function" then return false, "unavailable" end
    return native(topic, subject, value)
end
function M.messages.forget(subject)
    if type(subject) ~= "number" or subject % 1 ~= 0 or subject < 0 or subject > 4294967295 then
        return false, "expected_subject_uint32"
    end
    local native = rawget(_G, "cc2_bridge_messages_forget")
    if type(native) ~= "function" then return false, "unavailable" end
    return native(subject)
end

local event_names = {waypoints = true, airfield_tick = true,
    hangar_choice = true, island_controller = true, island_spawn = true,
    vehicle_controls = true, vehicle_control_query = true}
local event_handlers = {}
function M.events.available(name)
    local native = rawget(_G, "cc2_bridge_event_capabilities")
    return event_names[name] == true and type(native) == "table" and native[name] == true
end
function M.events.on(name, handler)
    if not event_names[name] or type(handler) ~= "function" then
        return false, "expected_event_name_and_function"
    end
    if not M.events.available(name) then return false, "unavailable" end
    local subscribe = rawget(_G, "cc2_bridge_event_subscription")
    if type(subscribe) == "function" and subscribe(name, true) ~= true then return false, "unavailable" end
    event_handlers[name] = handler
    return true
end
function M.events.off(name)
    event_handlers[name] = nil
    local subscribe = rawget(_G, "cc2_bridge_event_subscription")
    if type(subscribe) == "function" then subscribe(name, false) end
end
-- Called only by the shared Host on the authoritative game thread. Numeric
-- object IDs are copied; callers resolve checked cc2.world selectors as needed.
function _G.__cc2_engine_event(name, a, b, c)
    local handler = event_handlers[name]
    if handler then return handler(a, b, c) end
end

local names = {}

-- Executed once in the isolated declaration VM before game definitions load.
-- On session load the same call resolves that declaration; it never allocates
-- another engine ID or mutates the process-lifetime catalog.
-- Optional descriptor.order is a 0..65535 deterministic migration rank. Ranked
-- entries sort first; unranked entries sort by namespaced identity. It does not
-- pin an exact numeric definition ID or make changed-catalog saves compatible.
function M.vehicles.register(descriptor)
    if type(descriptor) ~= "table" then return nil, "expected_descriptor_table" end
    local native = rawget(_G, "cc2_bridge_vehicle_register")
    if type(native) ~= "function" then return nil, "early_registration_unavailable" end
    return native(descriptor)
end

function M.vehicles.lookup(identity)
    if type(identity) ~= "string" then return nil, "expected_namespaced_identity" end
    local native = rawget(_G, "cc2_bridge_vehicle_lookup")
    if type(native) ~= "function" then return nil, "definition_registry_unavailable" end
    return native(identity)
end

function M.vehicles.list()
    local native = rawget(_G, "cc2_bridge_vehicle_list")
    if type(native) ~= "function" then return {}, "definition_registry_unavailable" end
    return native()
end

function M.vehicles.is_ready(identity)
    local record, reason = M.vehicles.lookup(identity)
    if not record then return false, reason or "unknown_type" end
    return record.ready
end

-- Queue the existing, server-validated carrier hangar operation by stable
-- namespaced identity. The caller never needs to persist a process-local
-- definition ID; only ready definitions can be queued by the native host.
function M.vehicles.spawn_in_hangar(options)
    local function uint32(value, allow_zero)
        return type(value) == "number" and value == value and value % 1 == 0 and
            value >= (allow_zero and 0 or 1) and value <= 4294967295
    end
    if type(options) ~= "table" or type(options.type) ~= "string" or options.type == "" or
        not uint32(options.carrier, false) or not uint32(options.slot, true) then
        return nil, "expected_type_identity_carrier_id_and_slot"
    end
    local native = rawget(_G, "cc2_bridge_vehicle_spawn_in_hangar")
    if type(native) ~= "function" then return nil, "unavailable" end
    return native(options.type, options.carrier, options.slot)
end
for name, value in pairs(_G) do
    if type(name) == "string" and name:match("^update_") and type(value) == "function" then
        M.game_api[name] = value
        names[#names + 1] = name
    end
end
table.sort(names)

function M.list_game_api()
    local result = {}
    for i, name in ipairs(names) do
        result[i] = name
    end
    return result
end

function M.has_game_api(name)
    return type(name) == "string" and M.game_api[name] ~= nil
end

-- Generic virtual-key state supplied by the native host. Bindings and
-- gameplay policy stay in Lua; C++ only samples the requested key.
function M.input.key_down(virtualKey)
    if type(virtualKey) ~= "number" or virtualKey % 1 ~= 0 or virtualKey < 1 or virtualKey > 255 then
        return false, "unsupported_key"
    end
    local native = rawget(_G, "cc2_bridge_key_down")
    if type(native) ~= "function" then return false, "unavailable" end
    return native(virtualKey), "sampled"
end

-- These callbacks belong to the Core Bridge, not to the game's stock API.
-- They are registered by the verified native hook when the supported bridge
-- DLL and executable are present.
M.bridge.abi_version = rawget(_G, "cc2_bridge_abi_version")
M.bridge.bootstrap_status = rawget(_G, "cc2_bridge_bootstrap_status")
M.bridge.is_ready = rawget(_G, "cc2_bridge_is_ready")
M.bridge.build_id = rawget(_G, "cc2_bridge_build_id")
M.bridge.reload_mods = rawget(_G, "cc2_bridge_reload_mods")
M.bridge.is_available = type(M.bridge.abi_version) == "function"
    and type(M.bridge.bootstrap_status) == "function"
    and type(M.bridge.is_ready) == "function"
    and type(M.bridge.build_id) == "function"

-- Compatibility introspection only: the old diagnostic probe globals are not
-- registered by this CC2X build, so diagnostics.is_available is false.
M.bridge.diagnostics = {
    probe = rawget(_G, "cc2_bridge_probe"),
    number = rawget(_G, "cc2_bridge_number"),
    string = rawget(_G, "cc2_bridge_string"),
    echo_u32 = rawget(_G, "cc2_bridge_echo_u32"),
}
M.bridge.diagnostics.is_available = type(M.bridge.diagnostics.probe) == "function"
    and type(M.bridge.diagnostics.number) == "function"
    and type(M.bridge.diagnostics.string) == "function"
    and type(M.bridge.diagnostics.echo_u32) == "function"

-- Relative throttle step queued by the Bridge and applied on the next
-- c_vehicle_server_carrier update. It does not use type-5 input packets or the
-- helm controller-key. Lua mods decide when to request the primitive.
-- Discrete -1/0/+1 only; the bool reports queue acceptance, not physics outcome.
function M.carrier.throttle(value)
    if type(value) ~= "number" or (value ~= -1 and value ~= 0 and value ~= 1) then
        return false, "unsupported_value"
    end
    local native = rawget(_G, "cc2_bridge_apply_throttle_input")
    if type(native) ~= "function" then return false, "unavailable" end
    -- The audited native wrapper accepts uint32, so transport the three
    -- discrete values as nonnegative tokens. No float ABI is invented.
    if native(value + 1) == true then return true, "queued" end
    return false, "unavailable_or_busy"
end

-- Queue a chassis creation in an explicitly selected carrier hangar slot.
-- The engine adapter validates the live carrier, slot compatibility/occupancy,
-- build fingerprint and session again on the server tick. This is not the
-- stock workshop purchase path and does not debit workshop inventory.
function M.carrier.spawn_in_hangar(carrier_id, slot, chassis_type)
    local function uint32(value, allow_zero)
        return type(value) == "number" and value == value and value % 1 == 0 and
            value >= (allow_zero and 0 or 1) and value <= 4294967295
    end
    if not uint32(carrier_id, false) or not uint32(slot, true) or not uint32(chassis_type, true) then
        return false, "expected_carrier_id_slot_and_chassis_type"
    end
    local native = rawget(_G, "cc2_bridge_hangar_spawn")
    if type(native) ~= "function" then return false, "unavailable" end
    local accepted, reason, request_id = native(carrier_id, slot, chassis_type)
    if accepted == true then return true, reason or "queued", request_id end
    return false, "no_active_session_or_queue_busy"
end

-- Copy the carrier's current slot occupancy and type compatibility on the
-- Bridge server tick. A later spawn request revalidates both conditions.
function M.carrier.hangar_slots(carrier_id, chassis_type)
    local function uint32(value, allow_zero)
        return type(value) == "number" and value == value and value % 1 == 0 and
            value >= (allow_zero and 0 or 1) and value <= 4294967295
    end
    if not uint32(carrier_id, false) or not uint32(chassis_type, true) then
        return nil, "expected_carrier_id_and_chassis_type"
    end
    local native = rawget(_G, "cc2_bridge_hangar_slots")
    if type(native) ~= "function" then return nil, "unavailable" end
    return native(carrier_id, chassis_type)
end

-- Returns nil, "pending_or_expired" until the shared native backend reports a result.
function M.carrier.hangar_spawn_result(request_id)
    if type(request_id) ~= "number" or request_id < 1 or request_id % 1 ~= 0 or request_id > 4294967295 then
        return nil, "request_id_must_be_positive_uint32"
    end
    local native = rawget(_G, "cc2_bridge_hangar_spawn_result")
    if type(native) ~= "function" then return nil, "unavailable" end
    return native(request_id)
end

-- Typed counterpart to the compatibility carrier API above. Result shape and
-- lifecycle are shared; the returned vehicle_id is a copied stable ID.
M.vehicles.hangar_spawn_result = M.carrier.hangar_spawn_result

-- Compatibility name for the old player's helm-axis diagnostic transport.
-- That optional native transport is not ported to CC2X; returns unavailable.
function M.carrier.helm_throttle_input(value)
    if type(value) ~= "number" or (value ~= -1 and value ~= 0 and value ~= 1) then
        return false, "unsupported_value"
    end
    local native = rawget(_G, "cc2_bridge_helm_throttle_input")
    if type(native) ~= "function" then return false, "unavailable" end
    if native(value + 1) == true then return true, "queued" end
    return false, "unavailable_or_busy"
end

-- Movement primitives control four independent native animation groups.
-- Multipliers are integer-only and x1 restores stock coefficients; this does
-- not uninstall the fixed code hooks.
local function animationSpeed(name, value)
    if type(value) ~= "number" or value ~= value or value < 1 or value > 4294967295 or value % 1 ~= 0 then
        return false, "unsupported_value"
    end
    local native = rawget(_G, name)
    if type(native) ~= "function" then return false, "unavailable" end
    if native(value) == true then return true, "applied" end
    return false, "not_ready"
end
function M.animation.routes(value) return animationSpeed("cc2_bridge_anim_routes", value) end
function M.animation.runway(value) return animationSpeed("cc2_bridge_anim_runway", value) end
function M.animation.lift(value) return animationSpeed("cc2_bridge_anim_lift", value) end
function M.animation.crane(value) return animationSpeed("cc2_bridge_anim_crane", value) end

-- Metadata-only user logic type registry. This does not create or register a
-- native engine type and does not validate the game's definition catalog.
function M.entities.register(id, descriptor)
    if type(id) ~= "string" or type(descriptor) ~= "table" or
        type(descriptor.base) ~= "string" or type(descriptor.definition) ~= "string" then
        return false, "expected_id_and_descriptor_with_base_definition"
    end
    local native = rawget(_G, "cc2_bridge_entity_type_register")
    if type(native) ~= "function" then return false, "unavailable" end
    return native(id, descriptor.base, descriptor.definition)
end

function M.entities.count()
    local native = rawget(_G, "cc2_bridge_entity_type_count")
    if type(native) ~= "function" then return nil, "unavailable" end
    return native()
end

function M.entities.remove(id)
    local native = rawget(_G, "cc2_bridge_entity_type_remove")
    if type(native) ~= "function" then return false, "unavailable" end
    return native(id)
end

function M.entities.clear()
    local native = rawget(_G, "cc2_bridge_entity_type_clear")
    if type(native) ~= "function" then return nil, "unavailable" end
    return native()
end

function M.entities.types()
    local native = rawget(_G, "cc2_bridge_entity_types")
    if type(native) ~= "function" then return nil, "unavailable" end
    return native()
end

-- Stock facade methods retain their update_* names, tuples and effects.
-- Native carrier and movement primitives above retain their own contracts.
_G.cc2 = M
return M
