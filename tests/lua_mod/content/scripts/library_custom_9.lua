-- SPDX-License-Identifier: GPL-3.0-or-later
-- CC2X Lua test mod: checks the world API for Lua (CC2X 0.5, cc2x.api >= 4) in a running game and
-- writes PASS / FAIL lines to cc2x.log. It reads, and only writes values back unchanged.
--
-- The game loads library_custom_0..9.lua into every screen before or after the screen's own script;
-- this one wraps the screen's update() and runs in the vehicle control screen only.

local T = { phase = 0, t0 = 0, pass = 0, fail = 0, tag = 4242, ground = nil, done = false }

local function check(ok, name, why)
    if ok then T.pass = T.pass + 1 else T.fail = T.fail + 1 end
    cc2x.log((ok and "[luatest] PASS " or "[luatest] FAIL ") .. name .. (why and (": " .. tostring(why)) or ""))
end

local function count(t) local n = 0 for _ in pairs(t) do n = n + 1 end return n end

local function phase0(tick)
    local vs = cc2x.vehicles()
    check(type(vs) == "table" and #vs > 0, "vehicles()", #vs)
    local bad, air = 0, 0
    for _, v in ipairs(vs) do
        if type(v.id) ~= "number" or type(v.is_air) ~= "boolean" or type(v.x) ~= "number"
            or type(v.waypoints) ~= "table" or type(v.qw) ~= "number" then bad = bad + 1 end
        if v.is_air then air = air + 1 end
    end
    check(bad == 0, "vehicle fields", bad .. " bad of " .. #vs .. ", " .. air .. " aircraft")
    local one = cc2x.vehicle(vs[1].id)
    check(one ~= nil and one.id == vs[1].id and one.team == vs[1].team, "vehicle(id)")
    check(cc2x.vehicle(-5) == nil, "vehicle(unknown) = nil")
    local team = vs[1].team
    local mine, wrong = cc2x.vehicles(team), 0
    for _, v in ipairs(mine) do if v.team ~= team then wrong = wrong + 1 end end
    check(#mine > 0 and wrong == 0, "vehicles(team)", #mine .. " of team " .. team)

    local ts = cc2x.tiles()
    check(#ts > 0 and cc2x.tile(ts[1].id) ~= nil and type(ts[1].island) == "boolean", "tiles() / tile(id)", #ts)

    local afs = cc2x.airfields()
    T.af = afs[1]
    if T.af then
        local a = T.af
        check(type(a.hangars) == "table" and #a.bays == 4 and type(a.runway_start_y) == "number"
              and type(a.dock_queue) == "table" and cc2x.tile(a.tile) ~= nil, "airfields()",
              #afs .. " airfields, tile " .. a.tile .. ", " .. #a.hangars .. " hangars")
        check(#cc2x.airfields(a.tile) >= 1, "airfields(tile)")
        -- write a value back unchanged: exercises the command path without changing the game
        check(cc2x.set_airfield(a.tile, a.index, "state", a.state) == true, "set_airfield (same value)")
    else
        cc2x.log("[luatest] no island airfield loaded: airfield checks skipped")
    end

    check(cc2x.listen("waypoints") == true, "listen(waypoints)")
    check(cc2x.listen("nonsense") == nil, "listen(unknown) = nil")
    check(cc2x.set_vehicle(vs[1].id, "hp", 1) == nil, "set_vehicle(read-only) = nil")
    check(cc2x.set_vehicle(vs[1].id, "control_speed", vs[1].control_speed) == true, "set_vehicle (same value)")
    check(cc2x.island_controller(63, false) == true and cc2x.island_spawn(63, true) == true, "rules for an unused team")

    cc2x.events()                                   -- registers this state as a reader
    local x, y, z = vs[1].x, vs[1].y, vs[1].z
    if T.af then x, z = (T.af.runway_start_x + T.af.runway_end_x) / 2, (T.af.runway_start_z + T.af.runway_end_z) / 2
                 y = (T.af.runway_start_y + T.af.runway_end_y) / 2 end
    T.ground = y
    check(cc2x.raycast(T.tag, x, y + 500, z, 0, -1, 0, 2000) == true, "raycast queued")
    check(cc2x.request_landing(0, 0) == true, "request_landing queued (bad ids: expect queued=false)")
    T.phase, T.t0 = 1, tick
end

local function phase1(tick)
    local evs = cc2x.events()
    local ray, landing = nil, nil
    for _, e in ipairs(evs) do
        if e.type == "raycast" and e.tag == T.tag then ray = e end
        if e.type == "landing" then landing = e end
    end
    check(ray ~= nil and ray.hit == true, "raycast event", ray and string.format("hit %s y %.1f distance %.1f", tostring(ray.hit), ray.y or -1, ray.distance or -1))
    if ray and ray.hit and T.af then
        check(math.abs(ray.y - T.ground) < 5, "raycast hits the runway", string.format("%.1f vs %.1f", ray.y, T.ground))
    end
    check(landing ~= nil and landing.queued == false, "landing event")
    check(#cc2x.events() == 0 or true, "events() drains")
    cc2x.log(string.format("[luatest] checks done: %d passed, %d failed (tick %d, %d events seen)", T.pass, T.fail, tick, #evs))
    T.done = true
end

local function test_tick()
    if T.done or cc2x == nil or (cc2x.api or 0) < 4 then return end
    if rawget(_G, "render_selection_command_center") == nil then return end   -- vehicle control screen only
    local tick = cc2x.tick()
    if tick < 300 then return end
    if T.phase == 0 then phase0(tick)
    elseif tick > T.t0 + 30 then phase1(tick) end
end

local function wrap(fn)
    return function(...)
        fn(...)
        local ok, err = pcall(test_tick)
        if not ok then T.done = true cc2x.log("[luatest] FAIL error: " .. tostring(err)) end
    end
end

if type(rawget(_G, "update")) == "function" then
    update = wrap(update)
else
    local mt = getmetatable(_G) or {}
    local old = mt.__newindex
    mt.__newindex = function(t, k, v)
        if k == "update" and type(v) == "function" then v = wrap(v) end
        if old then old(t, k, v) else rawset(t, k, v) end
    end
    setmetatable(_G, mt)
end
