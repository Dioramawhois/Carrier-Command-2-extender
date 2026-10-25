# The world API (API 3, CC2X 0.4+)

The game's own objects - vehicles, tiles (map squares, each with one island), island controllers,
airfields, waypoints - read and changed through named fields and lists, plus physics, game functions
and game events. The SDK knows where everything lives in each supported game build: a plugin carries
no game addresses, so it keeps working when CC2X is updated for a new game build.

Island Airfields uses nothing else; it is the complete example. The exact list of fields is in
`include/cc2x.h` (`CC2X_WORLD_*`, `CC2X_VEHICLE_*`, `CC2X_TILE_*`, `CC2X_ISLAND_*`, `CC2X_AIRFIELD_*`,
`CC2X_WAYPOINT_*`).

```c
if (api->api_version < 3 || api->size < sizeof(cc2x_api)) return 0;   /* CC2X 0.4 or newer */
```

## Rules

- **Server thread only**: `on_server_tick`, `server_post`, `on_session` and the events below. Lua
  functions run on another thread: copy what Lua needs on the tick (see PLUGINS.md).
- **Keep ids, not pointers**, from one tick to the next: the game deletes objects at any tick.
  `find(CC2X_WORLD_VEHICLES, id)` gives the vehicle again (NULL = gone).
- Unknown field, wrong index, no session: reads give 0 / NULL, writes return 0. Fields marked `r`
  in the header are read-only (`seti` / `setf` refuse them).
- World axes: x and z horizontal, y up (height over the sea), metres. A vehicle's nose points along
  its local +z.

## Objects, fields, lists

```c
int n = api->count(NULL, CC2X_WORLD_VEHICLES);
for (int i = 0; i < n; ++i) {
    void *v = api->ref(NULL, CC2X_WORLD_VEHICLES, i);
    if (!api->geti(v, CC2X_VEHICLE_IS_AIR, 0) || api->geti(v, CC2X_VEHICLE_TEAM_IS_AI, 0)) continue;
    double x = api->getf(v, CC2X_VEHICLE_POS, 0), z = api->getf(v, CC2X_VEHICLE_POS, 2);
    void *tile = api->tile_at(x, z);
    api->log("my_plugin", CC2X_LOG_INFO, "aircraft %d over tile %d, fuel %.0f",
             (int)api->geti(v, CC2X_VEHICLE_ID, 0), (int)api->geti(tile, CC2X_TILE_ID, 0),
             api->getf(v, CC2X_VEHICLE_FUEL, 0));
}
```

- `geti` / `getf` read a field; `index` is the item of a list (`CC2X_AIRFIELD_HANGARS`, i) or the
  component of a vector (0..2 = x, y, z).
- `ref` gives an object: an item of a list of objects (`CC2X_WORLD_VEHICLES`,
  `CC2X_ISLAND_AIRFIELDS`, `CC2X_VEHICLE_WAYPOINTS`) or an object field (`CC2X_TILE_ISLAND`).
- `count` is the length of a list. Id lists (the airfield queues) also take `list_push`,
  `list_remove`, `list_clear`; `list_clear` empties the waypoints and the taxi path too.
- Island controllers exist only for loaded islands, and vanilla gives them only to AI teams
  (`on_island_controller` changes that). `CC2X_TILE_AIRFIELDS` remembers the count while the island
  is unloaded.

## Physics

```c
cc2x_body b;
if (api->body_get(v, &b)) {
    b.vel[1] += 1.0;                                   /* a little lift */
    api->body_set(v, &b, CC2X_BODY_VEL);               /* only the parts in `what`; wakes the body */
}
cc2x_hit hit;
double from[3] = {x, 2000, z}, down[3] = {0, -1, 0};
if (api->raycast(from, down, 3000, CC2X_RAY_STATIC, &hit)) ground = hit.pos[1];
```

- Orientation is a quaternion; `cc2x_quat_from_ypr(yaw, pitch, roll, q)` and `cc2x_yaw_of(dx, dz)`
  build one.
- **Never set the position of a vehicle that stands on its wheels**: the game destroys it. Lift it
  first, or set it down from above.
- `raycast` uses the PhysX scene of the game. With several scenes (a map with distant islands) it
  uses the scene of the vehicle nearest to the ray (logged once).

## Game functions

| | |
|---|---|
| `vehicle_brakes(v, brake, grip)`, `vehicle_gear(v, down)` | the vehicle's own controls |
| `request_landing(island, v)` | queue an aircraft at an island's airfield, as the game does for AI aircraft |
| `waypoints_sync(v)` | send changed waypoints to the clients |
| `airfield_to_world(af, local, world)` | airfield coordinates (x across, y up, z along the hangar row) to world |
| `airfield_bay(af, slot, pos, &yaw)` | parking bay of hangar slot 0..3 |

## Events

Call these from `cc2x_plugin_load`. The first subscriber installs the hook (logged as
`event <name>: hook installed`); without subscribers the game is not patched.

| | |
|---|---|
| `on_waypoints(fn)` | a vehicle got new waypoints (a player's order) |
| `on_airfield_tick(fn)` | after the game's tick of each island airfield |
| `on_hangar_choice(fn)` | an airfield picks the hangar slot for a landing aircraft: return a slot or -1 |
| `on_island_controller(fn)` | does a team's island get an island controller? return 1 to give one |
| `on_island_spawn(fn)` | an island controller spawns the island's defence units: return 0 to skip |

Several plugins can subscribe to one event; for the choices the first plugin with an answer wins.

## Checking it in your game

`python build.py` also builds `dist/cc2x_selftest.dll`. Put it next to `carrier_command.exe`, load a
save and wait ~20 s: `cc2x.log` gets a PASS / FAIL line for every part of the world API and counts
the events every minute. It only reads; remove it afterwards.
