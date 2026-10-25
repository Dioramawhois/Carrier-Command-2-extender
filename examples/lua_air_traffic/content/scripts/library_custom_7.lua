-- SPDX-License-Identifier: GPL-3.0-or-later
-- Air Traffic - an example Lua mod for CC2X (needs CC2X 0.5: cc2x.api >= 4).
--
-- On the vehicle control screen (the map table on the bridge) it shows a small box: how many of
-- your aircraft fly / stand, the one with the least fuel, and the last takeoffs and landings.
-- It replaces no game screen: the game loads library_custom_7.lua into every screen, and this file
-- adds its code to the screen's update().
--
-- Without CC2X the mod does nothing and the game runs as usual.

local REFRESH_TICKS = 30          -- read the world about once a second (30 server ticks), not every frame
local MAX_LINES = 3

local S = {
    tick = -1,                    -- server tick of the last refresh
    flight = {},                  -- vehicle id -> flight_state seen last time
    flying = 0, ground = 0,
    low_id = nil, low_fuel = nil,
    lines = {},                   -- newest first
}

local function is_vehicle_control_screen()
    -- every screen has update(); this function exists only in screen_vehicle_control.lua
    return rawget(_G, "render_selection_command_center") ~= nil
end

local function note(text)
    table.insert(S.lines, 1, text)
    if #S.lines > MAX_LINES then table.remove(S.lines) end
end

local function refresh(team)
    -- events first: they tell what happened between two refreshes
    for _, e in ipairs(cc2x.events()) do
        if e.type == "session" then
            S.flight, S.lines = {}, {}
        elseif e.type == "vehicle_removed" and S.flight[e.vehicle] ~= nil then
            note("#" .. e.vehicle .. " lost")
            S.flight[e.vehicle] = nil
        end
    end

    S.flying, S.ground, S.low_id, S.low_fuel = 0, 0, nil, nil
    for _, v in ipairs(cc2x.vehicles(team)) do
        if v.is_air then
            local before = S.flight[v.id]
            if before ~= nil and before ~= v.flight_state then
                if v.flight_state == 2 then note("#" .. v.id .. " took off")
                elseif before == 2 then note("#" .. v.id .. " landed") end
            end
            S.flight[v.id] = v.flight_state
            if v.flight_state == 2 then
                S.flying = S.flying + 1
                if S.low_fuel == nil or v.fuel < S.low_fuel then S.low_id, S.low_fuel = v.id, v.fuel end
            else
                S.ground = S.ground + 1
            end
        end
    end
end

local function draw(screen_w, screen_h)
    local text_color = color8(0, 255, 160, 255)
    local h = 10 + 10 * #S.lines
    local y = screen_h - 16 - h
    update_ui_rectangle(2, y, 150, h, color8(0, 0, 0, 180))
    local head = "AIR " .. S.flying .. " up / " .. S.ground .. " down"
    if S.low_id then head = head .. string.format("  low #%d %.0f", S.low_id, S.low_fuel) end
    update_ui_text(4, y + 1, head, 146, 0, text_color, 0)
    for i, line in ipairs(S.lines) do
        update_ui_text(4, y + 1 + 10 * i, line, 146, 0, color8(200, 200, 200, 255), 0)
    end
end

local function after_update(screen_w, screen_h, ticks)
    if cc2x == nil or (cc2x.api or 0) < 4 then return end      -- no CC2X, or older than 0.5
    if not is_vehicle_control_screen() then return end
    local tick = cc2x.tick()
    if tick == 0 then return end                               -- not in a game
    if S.tick < 0 or tick - S.tick >= REFRESH_TICKS or tick < S.tick then
        S.tick = tick
        refresh(update_get_screen_team_id())
    end
    draw(screen_w, screen_h)
end

-- Add after_update to the screen's own update(). The screen script may be loaded before or after
-- this library, so handle both. pcall keeps an error in this mod from breaking the screen.
local failed = false
local function wrap(original)
    return function(screen_w, screen_h, ticks)
        original(screen_w, screen_h, ticks)
        if failed then return end
        local ok, err = pcall(after_update, screen_w, screen_h, ticks)
        if not ok then
            failed = true
            if cc2x then cc2x.log("[air_traffic] stopped after an error: " .. tostring(err)) end
        end
    end
end

if type(rawget(_G, "update")) == "function" then
    update = wrap(update)
else
    local mt = getmetatable(_G) or {}
    local previous = mt.__newindex
    mt.__newindex = function(t, k, v)
        if k == "update" and type(v) == "function" then v = wrap(v) end
        if previous then previous(t, k, v) else rawset(t, k, v) end
    end
    setmetatable(_G, mt)
end
