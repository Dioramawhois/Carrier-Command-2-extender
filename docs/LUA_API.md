# CC2X for Lua (Workshop) mods

When the player has CC2X installed, every game Lua script (screens, the same environment your
Workshop mod already runs in) gets a global table `cc2x`. Without CC2X it is `nil`, so always
feature-test:

```lua
if cc2x and cc2x.api >= 1 then
    cc2x.log("my mod sees CC2X", cc2x.version)
end

-- a plugin feature
if cc2x and cc2x.has("island_airfields") then
    local n = cc2x.island_airfields.count(tile:get_id())
end
```

Your mod keeps working for players without CC2X; it just skips the extras.

## Core (always present with CC2X)

| | |
|---|---|
| `cc2x.version` | SDK version string, e.g. `"0.2.0"` |
| `cc2x.api` | API level (integer). New functions raise it; existing ones are not changed. |
| `cc2x.game_build` | `"1.5.18-win32"` |
| `cc2x.has(name)` | `true` if a plugin registered the feature `name` |
| `cc2x.log(...)` | writes its arguments to `cc2x.log` in the game folder (handy for debugging) |
| `cc2x.vehicles()` | array of all vehicles of the server, see below |
| `cc2x.vehicle(id)` | one vehicle or `nil` |
| `cc2x.tiles()` | array of all tiles (islands) |
| `cc2x.tile(id)` | one tile or `nil` |

Vehicle: `id, definition, team, dock_state, attached_to, hp, fuel, x, y, z`
(server truth: all vehicles of all teams, also the ones your screen cannot see).
Tile: `id, team, x, z, airfields` (`airfields` only while the island is loaded).

The data comes from the game *server*, refreshed ~3 times per second, so it exists only in
single player and for the host. On a client of somebody else's game the arrays are empty.

Errors: CC2X functions never raise Lua errors; they return `nil` (or `false`) when something is
not available.

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
