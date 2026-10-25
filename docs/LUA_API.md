# CC2X for Lua (Workshop) mods

When the player has CC2X installed, every game Lua script (screens, the same environment your
Workshop mod already runs in) gets a global table `cc2x`. Without CC2X it is `nil`, so always
feature-test:

```lua
if cc2x and cc2x.api >= 4 then
    cc2x.log("my mod sees CC2X", cc2x.version)
end

-- a plugin feature
if cc2x and cc2x.has("island_airfields") then
    local n = cc2x.island_airfields.count(tile:get_id())
end
```

Your mod keeps working for players without CC2X; it just skips the extras.

Everything here is the server's truth: all vehicles of all teams, also the ones your screen cannot
see. It exists in single player and for the host; on a client of somebody else's game the lists
are empty and commands do nothing.

CC2X functions never raise Lua errors: they return `nil, "reason"` (or `false`) when something is
not available.

## Basics

| | |
|---|---|
| `cc2x.version` | SDK version string, e.g. `"0.5.0"` |
| `cc2x.api` | API level (integer). New functions raise it; existing ones are not changed. 4 = everything below |
| `cc2x.game_build` | `"1.5.18-win32"` |
| `cc2x.has(name)` | `true` if a plugin registered the feature `name` |
| `cc2x.log(...)` | writes its arguments to `cc2x.log` in the game folder |
| `cc2x.tick()` | server tick of the data below (0 = no session) |

## Reading the world

The world is copied every server tick; reading never touches the game.

| | |
|---|---|
| `cc2x.vehicles([team])` | array of vehicles (all, or of one team) |
| `cc2x.vehicle(id)` | one vehicle or `nil` |
| `cc2x.tiles()` | array of tiles (map squares, each with one island) |
| `cc2x.tile(id)` | one tile or `nil` |
| `cc2x.airfields([tile_id])` | array of the airfields of loaded islands (all, or of one tile) |

**Vehicle**: `id, definition` (e_game_object_type), `team, team_is_ai, is_air, is_wing, is_rotor,
hp, fuel`; position `x, y, z` (y up, metres), orientation `qx, qy, qz, qw` and heading `yaw`
(radians, 0 = nose along +z), velocity `vx, vy, vz, speed`, spin `spin_x, spin_y, spin_z`;
`attached_to` (carrier id, 0 = none), `dock_state` (e_vehicle_dock_state), `dock_vehicle,
dock_tile, dock_airfield, flight_state` (0 parked, 1 ground, 2 flying); autopilot `control_mode,
control_x, control_y, control_z, control_speed, control_power, control_taxi`;
`waypoints` = array of `{type, param}` (e_waypoint_type; param = tile id for barge loading).

**Tile**: `id, team, x, y, z, island` (`true` while the island is loaded, i.e. has its island
controller), `airfields` (count; missing while not known yet).

**Airfield**: `tile, index` (0-based, as `dock_airfield` counts), origin `x, y, z` and `yaw` of the
hangar row; runway `runway_start_x/y/z`, `runway_end_x/y/z`; `state, mode, runway_vehicle`
(-1 = none); `hangars` = array of vehicle ids per slot (0 = free; slot `s` is `hangars[s + 1]`);
`dock_queue`, `undock_queue` = arrays of vehicle ids; `bays` = 4 parking bays `{x, y, z, yaw}`.

## Events

`cc2x.events()` returns what happened since **this script's** last call (each screen keeps its own
place; the first call starts it). Call it once per `update`.

| `type` | fields |
|---|---|
| `"session"` | `started` (true / false) |
| `"vehicle_added"` | `vehicle, definition, team` |
| `"vehicle_removed"` | `vehicle` |
| `"waypoints"` | `vehicle, count` - a vehicle got new waypoints (a player's order). Needs `cc2x.listen("waypoints")` |
| `"island_spawn"` | `tile, team, ai_team` - an island spawns its defence units. Needs `cc2x.listen("island_spawn")` |
| `"raycast"` | `tag, hit`, and if hit `x, y, z, nx, ny, nz, distance` - the answer to `cc2x.raycast` |
| `"landing"` | `vehicle, tile, queued` - the answer to `cc2x.request_landing` |
| `"overflow"` | `lost` - you did not call `events()` for a long time and missed that many |

`cc2x.listen(name)` turns on an event that needs a hook in the game (installed at the next server
tick, only when somebody asks for it).

## Changing the world

Commands are queued and carried out at the next server tick, in order. They return `true` when
queued; an id that does not exist any more is ignored.

| | |
|---|---|
| `cc2x.set_vehicle(id, field, value [, index])` | `dock_state, dock_vehicle, dock_tile, dock_airfield, flight_state, control_mode, control_x, control_y, control_z, control_speed, control_power, control_taxi`, `attachment_flying` (index = attachment) |
| `cc2x.set_airfield(tile_id, index, field, value [, slot])` | `state, mode, runway_vehicle`, `hangar` (slot 0..3: vehicle id, 0 = free) |
| `cc2x.set_waypoint(id, index, type [, param])` | change waypoint `index` (0 = first) of a vehicle; sent to the clients |
| `cc2x.clear_waypoints(id)` | remove all waypoints of a vehicle |
| `cc2x.set_position(id, x, y, z)` | move the vehicle's body. **Never move a vehicle that stands on its wheels**: the game destroys it. Lift it first or set it down from above |
| `cc2x.set_rotation(id, yaw, pitch, roll)` | radians; nose up and left side up are positive |
| `cc2x.set_velocity(id, vx, vy, vz)`, `cc2x.set_spin(id, x, y, z)` | m/s, rad/s |
| `cc2x.brakes(id, brake [, grip])`, `cc2x.gear(id, down)` | 0..1; `true` = gear down |
| `cc2x.request_landing(id, tile_id)` | the game's landing request at an island airfield (answer: event `landing`) |
| `cc2x.queue_push(tile_id, index, "dock" or "undock", id)`, `cc2x.queue_remove(...)` | the airfield's landing / takeoff queues |
| `cc2x.raycast(tag, x, y, z, dx, dy, dz, max_dist [, "static" / "dynamic" / "all"])` | first hit along a ray (static = terrain and buildings); answer: event `raycast` with your `tag` |

## Rules for the game's decisions

Some things the game decides in the middle of its own code, where Lua cannot answer in time. Set a
rule instead; it stays until you change it.

| | |
|---|---|
| `cc2x.island_controller(team, on)` | islands of `team` get an island controller (vanilla: AI teams only). It runs airfields, repairs and defence spawns |
| `cc2x.island_spawn(team, allowed)` | `false`: islands of `team` spawn no defence units |
| `cc2x.hangar(vehicle_id, slot)` | the hangar slot (0..3) this aircraft gets when it lands; `nil` removes the rule |

## Running code every frame without replacing a screen

The game loads `scripts/library_custom_0.lua` .. `library_custom_9.lua` into every screen; they are
empty, meant for mods. A mod can ship one of them and add its code to the screens' `update`
without replacing any screen (other mods that change the same screen keep working). Example:
`tests/lua_mod` in the CC2X repository. Two mods using the same `library_custom_N` replace each
other, so pick an unusual number and say which one in your mod's description.

## Plugin functions

Plugins add sub-tables: `cc2x.<plugin>.<function>`. Check with `cc2x.has("<plugin>")` first.

### island_airfields (Island Airfields mod)

| | |
|---|---|
| `count(tile_id)` | airfields of the island, `nil` = not known yet (island never loaded this session) |
| `planes()` | `true` if fixed-wing landings are enabled by the player |
| `land(vehicle_id, tile_id)` | queue an aircraft for landing on an own island (next server tick); `true` = request sent |

A landing can also be ordered without CC2X calls: give the aircraft one waypoint and set
`vehicle:set_waypoint_type_barge_load_tile(waypoint_id, tile_id)` - the plugin turns it into a
landing (this also works for orders made by other mods).
