# Making Carrier Command 2 mods with CC2X - the complete guide

This guide takes you from nothing to a published Steam Workshop mod that reads and changes the live
game world. It covers both ways CC2X offers: **Lua mods** (no compiler, the usual Workshop mod plus
the `cc2x` table) and **native plugins** (C, for things that must run inside the game's own code).

Reference pages, for when you know what you are looking for:

- [LUA_API.md](LUA_API.md) - every `cc2x.*` function for Lua
- [WORLD_API.md](WORLD_API.md) - the world API for C plugins
- [PLUGINS.md](PLUGINS.md) - plugin basics, patching, threads
- `include/cc2x.h` - the complete C API in one header

Contents

1. [What CC2X is](#1-what-cc2x-is)
2. [Lua mod or C plugin?](#2-lua-mod-or-c-plugin)
3. [Setting up](#3-setting-up)
4. [How a Carrier Command 2 mod is built](#4-how-a-carrier-command-2-mod-is-built)
5. [Your first Lua mod, step by step](#5-your-first-lua-mod-step-by-step)
6. [Reading the world](#6-reading-the-world)
7. [Events](#7-events)
8. [Changing the world](#8-changing-the-world)
9. [Rules for the game's own decisions](#9-rules-for-the-games-own-decisions)
10. [A complete example: Air Traffic](#10-a-complete-example-air-traffic)
11. [Your first C plugin](#11-your-first-c-plugin)
12. [The world API in C](#12-the-world-api-in-c)
13. [Patching the game yourself](#13-patching-the-game-yourself)
14. [Testing and debugging](#14-testing-and-debugging)
15. [Publishing on the Steam Workshop](#15-publishing-on-the-steam-workshop)
16. [Rules that keep mods working](#16-rules-that-keep-mods-working)
17. [Troubleshooting](#17-troubleshooting)

---

## 1. What CC2X is

Carrier Command 2 already supports Workshop mods: a mod is a folder whose files replace the game's
own files (Lua scripts of the screens, vehicle definitions, models...). What the game does not give
mods is access to the **game server**: the real positions of all vehicles, the island airfields,
the physics, the moments when the game decides something. Screen scripts only see what the screens
show.

CC2X is one file, `dinput8.dll`, that the player puts into the game folder once. The game loads it
by itself at start. From then on:

- every game Lua script has a global table **`cc2x`** with the live world, events, commands and
  rules (section 5 onwards);
- mods can bring **native plugins** (`.dll` files inside the mod) that run inside the game process
  with a C API (section 11 onwards);
- everything that depends on the exact game version (addresses, offsets, hooks) lives in CC2X.
  Your mod contains no game addresses, so it keeps working when CC2X is updated for a new game
  version.

Supported: Carrier Command 2 **1.5.18, Windows**. On an unknown game version CC2X does nothing and
says so in `cc2x.log`; the game runs normally. Multiplayer: the world data and commands exist on the
machine that runs the game server (single player or the host).

Players install CC2X once; it is shared by all mods that use it. Mods without CC2X keep running -
they just do not get the `cc2x` table, so a mod can show a hint (section 15.5).

## 2. Lua mod or C plugin?

| You want to... | Lua mod | C plugin |
|---|---|---|
| read vehicles, islands, airfields of all teams | yes | yes |
| react to events (new orders, vehicles appearing / disappearing, island spawns) | yes, next frame | yes, at once |
| change vehicle / airfield fields, waypoints, move bodies, brakes, gear, ray casts | yes, next server tick | yes, at once |
| decide in the middle of the game's code (hangar slot, island controller, spawns) | yes, by setting a rule beforehand | yes, with a callback |
| fly a vehicle every tick precisely (autopilot) | possible but one frame late | yes |
| hook a game function CC2X does not cover yet | no | yes (section 13) |
| draw on the screens | yes (game UI functions) | via a Lua function you register |
| ship without a compiler | yes | no |

Start with Lua. Move a part to C only when Lua cannot do it. Island Airfields is an example of a
mixed mod: its flight director is a C plugin, its map markers and orders are Lua.

## 3. Setting up

### 3.1 What you need

- Carrier Command 2 (Steam), Windows.
- CC2X: download [dinput8.dll](https://github.com/Dioramawhois/cc2x/releases/latest/download/dinput8.dll)
  and put it into the game folder (Steam: right-click Carrier Command 2 > Manage > Browse local
  files). Version 0.5 or newer for everything in this guide.
- A text editor. For C plugins also Python 3 and the CC2X repository (section 11.1).
- The game's own scripts for reference: they ship with the game in
  `<game folder>\mod_dev_kit\source\scripts` (`library_enum.lua` has every enum, the `screen_*.lua`
  files show how the screens use the game's Lua functions).

### 3.2 Where things are

| | |
|---|---|
| `<game folder>\cc2x.log` | CC2X's log, rewritten at every game start. Your `cc2x.log(...)` lines land here |
| `%APPDATA%\Carrier Command 2\mods\<your mod>` | local mods (your development copy) |
| `%APPDATA%\Carrier Command 2\mods.xml` | the list of enabled mods (the game writes it from the Mods menu) |
| `%APPDATA%\Carrier Command 2\settings.xml` | game settings (`gfx_fullscreen`) |
| `%APPDATA%\Carrier Command 2\cc2x\` | settings files of plugins (survive mod updates) |
| `<Steam>\steamapps\workshop\content\1489630\<item id>` | subscribed Workshop mods |
| `%LOCALAPPDATA%\CrashDumps` | Windows crash dumps (`carrier_command.exe.<pid>.dmp`) |

### 3.3 Developer comfort

- **Windowed mode** makes switching to the editor and taking screenshots easier: in
  `settings.xml` set `gfx_fullscreen="false"` while the game is closed.
- **Faster game time** for testing: create `<game folder>\cc2x.ini` with
  ```ini
  [cc2x]
  time_scale=4
  ```
  The server logic then runs 4 ticks for every normal one (1..8). Remove it before playing
  normally.
- After changing a Lua file or a plugin, restart the game. The server runs about 30 ticks per
  second; `cc2x.tick()` counts them.

## 4. How a Carrier Command 2 mod is built

```
My Mod/
    mod.xml                  name, author, description (shown in the Mods menu and on the Workshop)
    thumbnail.png            the picture (Workshop: under 1 MB)
    content/                 files that replace / add to the game's files
        scripts/...          Lua scripts
        game_objects/...     vehicle definitions (XML)
        cc2x/...             CC2X plugins (*.dll), section 11.5
```

`mod.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<data name="My Mod" author="me" description="What it does. Needs CC2X 0.5: https://github.com/Dioramawhois/cc2x/releases/latest/download/dinput8.dll">
</data>
```

A file in `content` with the same path as a game file **replaces** that game file. Two mods that
replace the same file conflict: only one wins. Replacing a whole screen script
(`content/scripts/screen_holomap.lua`) is therefore the last resort. CC2X mods have a better way.

### 4.1 The custom libraries: code in every screen without replacing one

The game loads these scripts into **every** screen, after its own libraries (`library_enum.lua`,
`library_util.lua`, `library_vehicle.lua`, `library_ui.lua`):

```
scripts/library_custom_0.lua ... scripts/library_custom_9.lua
```

They are empty in the game - slots meant for mods. Ship one of them and your code runs in every
screen, next to the screen's own code, while all screens stay the game's originals (and the ones
other mods changed). Your file can wrap the screen's `update()` function to run code every frame
(section 5.2).

Two mods that ship the same `library_custom_N.lua` replace each other. Pick a number, and say in
your mod description which one you use. Examples in the CC2X repository use 7 (Air Traffic) and 9
(the test mod).

### 4.2 Enabling a local mod

Put the folder into `%APPDATA%\Carrier Command 2\mods\`, start the game, open **Mods**, enable it,
restart the game (the Mods menu says so).

## 5. Your first Lua mod, step by step

Goal: a mod that writes a line to `cc2x.log` every 10 seconds with the number of your aircraft.

### 5.1 The folder

```
%APPDATA%\Carrier Command 2\mods\Hello CC2X\
    mod.xml
    content\scripts\library_custom_5.lua
```

`mod.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<data name="Hello CC2X" author="me" description="My first CC2X mod">
</data>
```

### 5.2 The script

`library_custom_5.lua`:

```lua
-- runs in every screen; we only want one screen: the vehicle control screen (the map table)
local function is_vehicle_control_screen()
    return rawget(_G, "render_selection_command_center") ~= nil
end

local last = 0

local function every_frame(screen_w, screen_h, ticks)
    if cc2x == nil or (cc2x.api or 0) < 4 then return end   -- no CC2X or older than 0.5
    if not is_vehicle_control_screen() then return end
    local tick = cc2x.tick()                                -- server tick, 0 = not in a game
    if tick == 0 or tick - last < 300 then return end       -- ~10 s at 30 ticks per second
    last = tick
    local team = update_get_screen_team_id()                -- the player's team (game function)
    local air = 0
    for _, v in ipairs(cc2x.vehicles(team)) do
        if v.is_air then air = air + 1 end
    end
    cc2x.log("hello: team", team, "has", air, "aircraft")
end

-- add every_frame to the screen's update(); the screen script may come before or after us
local function wrap(original)
    return function(screen_w, screen_h, ticks)
        original(screen_w, screen_h, ticks)
        local ok, err = pcall(every_frame, screen_w, screen_h, ticks)
        if not ok and cc2x then cc2x.log("hello: error " .. tostring(err)) end
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
```

What each part does:

- **Choosing a screen.** All screens get the file. A function that exists only in one screen script
  tells which screen this is: `render_selection_command_center` is defined only in
  `screen_vehicle_control.lua`. Look into the game's `screen_*.lua` for a unique function of the
  screen you want. Note that the bridge has **several copies** of some screens (the vehicle control
  screen exists three times), and each copy runs your code with its own variables.
- **Wrapping `update`.** Every screen defines `function update(screen_w, screen_h, ticks)` and the
  game calls it every frame. The wrapper calls the original first (the screen draws as usual), then
  your code. `pcall` makes sure an error in your mod never breaks the game's screen. The
  `__newindex` part handles a screen script that is loaded after the library: the moment it defines
  `update`, the wrapper is put around it.
- **Feature test.** `cc2x` is `nil` without CC2X, and `cc2x.api` tells the version of the Lua API
  (4 = CC2X 0.5).
- **Not every frame.** `cc2x.vehicles()` builds a Lua table per vehicle (100+ in a campaign).
  Calling it every frame works, but once a second is plenty for most mods. Use `cc2x.tick()` to
  count.

### 5.3 Run it

1. Start the game, Mods > enable "Hello CC2X", restart the game.
2. Load a save, wait 10 seconds.
3. Open `<game folder>\cc2x.log`:
   ```
   23:11:50.081 [lua] info: hello: team 1 has 4 aircraft
   ```

## 6. Reading the world

CC2X copies the world every server tick. Your script reads the copy, so reading is safe and never
disturbs the game. Full field lists: [LUA_API.md](LUA_API.md#reading-the-world).

```lua
-- all vehicles, or the ones of a team
for _, v in ipairs(cc2x.vehicles()) do ... end
for _, v in ipairs(cc2x.vehicles(update_get_screen_team_id())) do ... end

-- one vehicle by id (nil if it does not exist)
local v = cc2x.vehicle(12)
if v then
    cc2x.log(v.id, v.definition, v.x, v.y, v.z, v.speed, v.fuel, v.flight_state)
end
```

Useful vehicle fields:

| | |
|---|---|
| `definition` | the kind of vehicle: `e_game_object_type` from the game's `library_enum.lua` (`chassis_carrier` 0, `chassis_air_wing_light` 8, `chassis_air_wing_heavy` 10, `chassis_air_rotor_light` 12, `chassis_air_rotor_heavy` 14, `chassis_sea_barge` 16, ...) |
| `is_air, is_wing, is_rotor` | aircraft, plane, helicopter |
| `team, team_is_ai` | owner |
| `x, y, z` | position in metres; **y is up** (height over the sea), x and z are the map |
| `yaw` | heading in radians: 0 = nose towards +z, pi/2 = towards +x |
| `vx, vy, vz, speed` | velocity, m/s |
| `dock_state` | `e_vehicle_dock_state`: 0 undocked, 1 docking, 4 docked, 5 in the landing queue, ... |
| `attached_to` | id of the carrier the vehicle stands on (0 = none) |
| `dock_tile, dock_airfield` | the island airfield it lands at / parks in |
| `flight_state` | 0 parked, 1 on the ground, 2 flying |
| `waypoints` | its orders: `{ {type = 0, param = 0}, ... }` (`e_waypoint_type`) |

Tiles are the map squares; each holds one island:

```lua
for _, t in ipairs(cc2x.tiles()) do
    if t.team == update_get_screen_team_id() then
        cc2x.log("my island", t.id, "at", t.x, t.z, "airfields:", t.airfields)
    end
end
```

The game keeps an island **loaded** (with its island controller: airfields, repairs, defences) only
in some situations; `t.island` tells whether it is loaded now. Airfields are visible for loaded
islands:

```lua
for _, a in ipairs(cc2x.airfields()) do
    -- a.tile, a.index, runway a.runway_start_x/_y/_z .. a.runway_end_x/_y/_z,
    -- a.hangars = { vehicle id or 0 per slot }, a.dock_queue, a.undock_queue, a.bays (4 parking bays)
end
```

Map coordinates of the game's own screens: the map screens use `x` and `y` for the ground
plane; that `y` is the world's **z**. A vehicle at world `(x, y, z)` is at map `(x, z)`.

## 7. Events

Events tell what happened between two calls. Each script (each screen copy) has its own place in
the event stream: the first call starts it, every next call returns what is new.

```lua
cc2x.listen("waypoints")          -- once: this event needs a hook in the game

-- every frame (or every refresh)
for _, e in ipairs(cc2x.events()) do
    if e.type == "waypoints" then
        cc2x.log("vehicle", e.vehicle, "got", e.count, "waypoints")
    elseif e.type == "vehicle_removed" then
        cc2x.log("vehicle", e.vehicle, "is gone")
    elseif e.type == "session" and e.started then
        -- a game was loaded: forget everything you remembered about the previous one
    end
end
```

| event | when | needs `listen` |
|---|---|---|
| `session` | a game was loaded (`started = true`) or left | no |
| `vehicle_added` / `vehicle_removed` | a vehicle appeared / disappeared (built, spawned, destroyed, despawned). Not sent for the vehicles that exist when a game is loaded | no |
| `waypoints` | a vehicle got new orders | yes |
| `island_spawn` | an island controller spawns the island's defence units | yes |
| `raycast`, `landing` | answers to your `cc2x.raycast` / `cc2x.request_landing` | no |
| `overflow` | you missed events (more than 1024 since your last call) | no |

After loading a game, read the world once with `cc2x.vehicles()`; from then on the events tell
you what changes.

## 8. Changing the world

Lua runs on the screens' thread, the game world on the server thread. So a change is a
**command**: it is queued and carried out at the next server tick (a few milliseconds later), in
the order you sent them. The function returns `true` when the command was queued; a vehicle id
that no longer exists is ignored.

```lua
-- autopilot target and speed of a vehicle
cc2x.set_vehicle(id, "control_x", 31000)
cc2x.set_vehicle(id, "control_z", 9500)
cc2x.set_vehicle(id, "control_speed", 40)

-- waypoints: change the first waypoint's type, or remove them all
cc2x.set_waypoint(id, 0, e_waypoint_type.move)
cc2x.clear_waypoints(id)

-- physics of the vehicle's body
cc2x.set_velocity(id, 0, 5, 0)                 -- m/s, y up
cc2x.set_rotation(id, math.pi / 2, 0, 0)        -- yaw, pitch, roll in radians
cc2x.set_position(id, x, y, z)                  -- see the warning below
cc2x.brakes(id, 1.0)                            -- 0..1
cc2x.gear(id, true)                             -- gear down

-- island airfields (tile id + airfield index)
cc2x.set_airfield(tile_id, 0, "hangar", 0, 2)   -- free hangar slot 2
cc2x.queue_remove(tile_id, 0, "dock", id)       -- take an aircraft out of the landing queue
```

> **Never set the position of a vehicle that stands on its wheels.** The game destroys it. Lift it
> first (velocity up, or a position clearly above the ground) and let it come down.

Commands that answer do it with an event, matched by a number you choose (`tag`):

```lua
-- how high is the ground under (x, z)?
cc2x.raycast(17, x, 2000, z, 0, -1, 0, 3000)     -- tag 17, from 2000 m straight down, up to 3000 m

for _, e in ipairs(cc2x.events()) do
    if e.type == "raycast" and e.tag == 17 then
        if e.hit then cc2x.log("ground at", e.y, "slope normal", e.nx, e.ny, e.nz) end
    end
end
```

The answer arrives one or two frames later; keep your state between frames (local variables
outside the function).

## 9. Rules for the game's own decisions

Some things the game decides in the middle of its own code: which hangar slot a landing aircraft
gets, whether an island gets an island controller, whether an island spawns defence units. A Lua
script cannot answer in that moment (it runs on another thread), so you set a **rule** beforehand:

```lua
local me = update_get_screen_team_id()
cc2x.island_controller(me, true)    -- my islands get island controllers (vanilla: AI only)
cc2x.island_spawn(me, false)        -- ...but spawn no defence units
cc2x.hangar(vehicle_id, 3)          -- this aircraft parks in hangar slot 3 when it lands
cc2x.hangar(vehicle_id, nil)        -- remove that rule
```

Rules stay until you change them (also across loaded games while the game runs). The hook in the
game is installed the first time somebody sets such a rule.

When two mods set rules for the same thing, a C plugin's answer comes first, then the Lua rules.

## 10. A complete example: Air Traffic

`examples/lua_air_traffic` in the CC2X repository is a finished Lua mod built from sections 5-7:
on the vehicle control screen it draws a box with your aircraft in the air and on the ground, the
one with the least fuel, and the last takeoffs and landings. Read it top to bottom; it is
commented. The parts:

- **refresh about once a second** (`REFRESH_TICKS = 30`): events first (to notice lost aircraft and a
  new game), then one pass over `cc2x.vehicles(team)` that compares each aircraft's `flight_state`
  with the last pass to detect takeoffs and landings;
- **draw every frame** from the remembered state with the game's own UI functions
  (`update_ui_rectangle`, `update_ui_text`, `color8` - see the game's screen scripts for more);
- **stop on error**: the first error is logged once and the mod switches itself off, the screen
  keeps working.

To try it: copy the folder `examples/lua_air_traffic` to `%APPDATA%\Carrier Command 2\mods\`,
enable "Air Traffic (CC2X example)" in the Mods menu, restart the game, load a save and look at
the map table.

## 11. Your first C plugin

### 11.1 Tools

1. Python 3.
2. The CC2X repository: `git clone https://github.com/Dioramawhois/cc2x`.
3. A C compiler for 32-bit Windows. `build.py` uses Zig 0.15.2 as the compiler: download
   `https://ziglang.org/download/0.15.2/zig-x86_64-windows-0.15.2.zip` (sha256
   `3a0ed1e8799a2f8ce2a6e6290a9ff22e6906f8227865911fb7ddedc3cc14cb0c`) and unpack it to
   `cc2x\.deps\` so that `cc2x\.deps\zig-x86_64-windows-0.15.2\zig.exe` exists (or set `CC2X_ZIG`
   to your `zig.exe`). Any other compiler that makes 32-bit Windows DLLs works as well.
4. Check: `python build.py` in the `cc2x` folder builds `dist\dinput8.dll`, `dist\cc2x_hello.dll`
   and `dist\cc2x_selftest.dll`.

### 11.2 The smallest plugin

`examples/hello_plugin/hello.c`:

```c
#include <stdio.h>
#include "cc2x.h"

static const cc2x_api *api;
static int ticks;

static void on_tick(void *server, void *user) { ++ticks; }

static void on_session(void *server, int started, void *user) {
    api->log("hello", CC2X_LOG_INFO, "session %s after %d ticks", started ? "started" : "ended", ticks);
    ticks = 0;
}

/* Lua: cc2x.hello.greet(name) -> "hello, <name>! server ticks: N" */
static int greet(cc2x_lua *L) {
    const char *name = api->lua_tostring(L, 1, NULL);
    char text[128];
    snprintf(text, sizeof(text), "hello, %s! server ticks: %d", name ? name : "?", ticks);
    api->lua_pushstring(L, text);
    return 1;
}

CC2X_EXPORT int cc2x_plugin_load(const cc2x_api *a) {
    if (a->api_version < CC2X_API_VERSION) return 0;
    api = a;
    api->on_server_tick(on_tick, NULL);
    api->on_session(on_session, NULL);
    api->lua_add("hello", "greet", greet);
    api->lua_feature("hello");
    return 1;
}
```

- `cc2x_plugin_load` is the only export. CC2X calls it once at game start, **inside the Windows
  loader lock**: subscribe, register and return - no threads, no waiting, no `LoadLibrary` here.
  Return 1 to stay loaded, 0 to be ignored.
- **Version check.** `api->api_version` is the API level of the CC2X that runs. Check the lowest
  level your plugin needs (`>= 3` for the world API, `>= 4` for CC2X 0.5) and also
  `api->size >= sizeof(cc2x_api)`, so an older CC2X makes your plugin step back with a log line
  instead of crashing.
- **Threads.** `on_server_tick` and `on_session` run on the game's server thread: there you may
  use the world API. Lua functions you register run on the screens' thread: they must not touch
  game objects. Give Lua copies made on the tick (under your own lock), and send actions to the
  server thread with `api->server_post(fn, user)`.

### 11.3 Building your plugin in your own project

Keep your mod in its own repository next to `cc2x`, and call CC2X's build helper:

```python
# my_mod/build.py
import importlib.util, sys
from pathlib import Path

SDK = Path(__file__).resolve().parent.parent / 'cc2x'
spec = importlib.util.spec_from_file_location('cc2x_build', SDK / 'build.py')
cc2x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cc2x)

cc2x.plugin(Path('plugin/my_mod.c'), Path('dist/cc2x_my_mod.dll'))
```

`plugin(source, out)` compiles with the right flags for the game (32-bit, C11) and the CC2X
header. Island Airfields (`native/build.py`, `native/sdk_support.py`) is a complete example.

### 11.4 Lua and C together

A plugin adds functions to the `cc2x` table: `api->lua_add("my_mod", "count", fn)` makes
`cc2x.my_mod.count(...)`, and `api->lua_feature("my_mod")` makes `cc2x.has("my_mod")` true. Read
arguments with `api->lua_tointeger / lua_tonumber / lua_tostring / lua_toboolean`, push results
with `api->lua_push*`, build tables with `api->lua_newtable` + `lua_setfield` / `lua_appendi`.
Never raise Lua errors from C: return `nil` and a reason.

### 11.5 Shipping the plugin

Put the DLL into your mod: `content\cc2x\cc2x_my_mod.dll`. It must be inside `content`: the
game's Workshop upload takes only `mod.xml`, `thumbnail.png` and `content`. CC2X loads the
plugins of every **enabled** mod (local or Workshop) at game start; disabling the mod in the Mods
menu disables the plugin after a restart. For quick development you can also put
`cc2x_my_mod.dll` next to `carrier_command.exe`.

Settings that should survive mod updates (Steam replaces the mod folder on every update) belong in
`api->config_dir` (`%APPDATA%\Carrier Command 2\cc2x`). `api->plugin_dir()` is your plugin's own
folder while `cc2x_plugin_load` runs (for data files next to the DLL).

## 12. The world API in C

The same world as in Lua, but live and immediate, on the server thread. Full reference:
[WORLD_API.md](WORLD_API.md).

```c
static void on_tick(void *server, void *user) {
    int n = api->count(NULL, CC2X_WORLD_VEHICLES);
    for (int i = 0; i < n; ++i) {
        void *v = api->ref(NULL, CC2X_WORLD_VEHICLES, i);
        if (!api->geti(v, CC2X_VEHICLE_IS_AIR, 0)) continue;
        cc2x_body b;
        if (api->body_get(v, &b) && b.pos[1] < 20.0 && api->geti(v, CC2X_VEHICLE_FLIGHT_STATE, 0) == CC2X_FLIGHT_FLYING)
            api->log("low_alert", CC2X_LOG_WARN, "aircraft %d is %.0f m over the sea",
                     (int)api->geti(v, CC2X_VEHICLE_ID, 0), b.pos[1]);
    }
}
```

- Objects are `void *`; read and write them with named fields (`geti`, `getf`, `seti`, `setf`)
  and lists (`count`, `ref`). Unknown field or index: 0 / NULL, never a crash.
- **Keep ids, not pointers** from one tick to the next; find the object again with
  `api->find(CC2X_WORLD_VEHICLES, id)`.
- Game events with a callback: `on_waypoints`, `on_airfield_tick`, `on_hangar_choice`,
  `on_island_controller`, `on_island_spawn`. Subscribe in `cc2x_plugin_load`.

## 13. Patching the game yourself

When CC2X does not cover something yet, a plugin can hook the game directly (see
[PLUGINS.md](PLUGINS.md#patching)): every patch checks the original bytes, patches of different
plugins never overlap, and a failed commit changes nothing. Addresses are image addresses as
Ghidra shows them for base `0x00400000`; check `api->game_build` and refuse unknown builds.

Better: tell us what you needed (an issue on the CC2X repository). The goal is that mods carry no
game addresses: what one mod needs goes into CC2X, and every mod gets it.

## 14. Testing and debugging

### 14.1 cc2x.log

Everything CC2X does is in `<game folder>\cc2x.log`: the version, which plugins and hooks were
loaded, session start / end, your `cc2x.log` (Lua) and `api->log` (C) lines.

```
22:46:35.347 [cc2x] info: CC2X 0.5.0 ready for Carrier Command 2 1.5.18-win32
22:46:35.353 [cc2x] info: plugin cc2x_island_airfields.dll loaded (mod "Island Airfields")
22:46:52.985 [cc2x] info: session started
22:48:50.387 [cc2x] info: event waypoints: hook installed
22:47:12.328 [lua] info: hello: team 1 has 4 aircraft
```

### 14.2 Is CC2X itself working on this machine?

- `tests/lua_mod` ("CC2X Lua Test"): copy it to the mods folder, enable it, load a save. After
  ~10 s `cc2x.log` has PASS / FAIL lines for the whole Lua API and a summary
  `checks done: 20 passed, 0 failed`.
- `tests/selftest` (`dist\cc2x_selftest.dll`): put it next to `carrier_command.exe`, load a save,
  wait ~20 s: PASS / FAIL for the C world API. It only reads; remove it afterwards.

### 14.3 When Lua code fails

The `pcall` in the wrapper (section 5.2) catches the error and you log it. Common causes:
calling a game function that the screen does not have, `nil` fields (a vehicle without a body
has no `speed`... check with `if v.speed then`), forgetting `cc2x.api` checks.

### 14.4 When the game crashes

A crash dump appears in `%LOCALAPPDATA%\CrashDumps`. The last lines of `cc2x.log` tell what
happened just before. If the crash is in a plugin, the faulting module in the dump is your DLL.
Report CC2X crashes with `cc2x.log` and the dump.

## 15. Publishing on the Steam Workshop

### 15.1 Before uploading

- `mod.xml`: a clear name, a description that says **"Needs CC2X 0.5 or newer"** with the
  download link.
- `thumbnail.png` **under 1 MB** (the Workshop refuses bigger previews). 512-640 px square PNG
  is fine.
- Plugins inside `content\cc2x\`. Nothing outside `mod.xml`, `thumbnail.png` and `content` is
  uploaded.
- Test the exact folder you upload: enable it as a local mod, restart, load a save.

### 15.2 Upload from the game

Mods menu > **Upload to Workshop** > pick your local mod > visibility > **New workshop item**
(later: **Update existing item**) > accept. Start with visibility *private*, check the page, then
make it public on the Workshop page.

### 15.3 When the in-game upload fails: SteamCMD

The game's uploader sometimes stops with "failed to copy content". Valve's command line tool
SteamCMD uploads the same folder without the game:

1. Download https://steamcdn-a.akamaihd.net/client/installer/steamcmd.zip and unpack it into a
   folder of its own.
2. Write `my_mod.vdf` next to it (use `\\` in paths):
   ```
   "workshopitem"
   {
       "appid"            "1489630"
       "publishedfileid"  "0"
       "contentfolder"    "C:\\path\\to\\My Mod"
       "previewfile"      "C:\\path\\to\\My Mod\\thumbnail.png"
       "visibility"       "2"
       "title"            "My Mod"
       "description"      "What it does. Needs CC2X 0.5..."
       "changenote"       "First version"
   }
   ```
   `publishedfileid` 0 creates a new item; `visibility` 2 = private (0 public, 1 friends, 3
   unlisted). The content folder is the mod folder itself (with `mod.xml` in it).
3. Run in a command prompt:
   ```
   steamcmd.exe +login YOUR_STEAM_LOGIN +workshop_build_item "C:\path\to\my_mod.vdf" +quit
   ```
   It asks for your password and Steam Guard code, uploads, and prints the new item's
   `PublishedFileId`.
4. For updates put that id into `publishedfileid` and leave out `visibility` (otherwise a public
   item goes private again). SteamCMD writes the id into the `.vdf` itself after the first upload.

### 15.4 The Workshop page

- The description takes BBCode: `[h1]`, `[h2]`, `[b]`, `[list][*]...[/list]`, `[olist]`,
  `[url=...]...[/url]`, `[code]...[/code]`; up to 8000 characters.
- **Videos** cannot be uploaded: put the video on YouTube and add the link on the item page
  (Add/edit images & videos > YouTube). Images and animated GIFs can be uploaded there.
- Say which CC2X version the mod needs and which `library_custom_N` it uses.

### 15.5 Helping players without CC2X

Players who subscribe without installing CC2X get your mod without the `cc2x` table. Tell them on
the screen they look at. Island Airfields draws one yellow line at the bottom of the map:

```lua
if cc2x == nil then
    update_ui_rectangle(0, screen_h - 13, screen_w, 13, color8(0, 0, 0, 220))
    update_ui_text(4, screen_h - 11, "My Mod needs CC2X: github.com/Dioramawhois/cc2x", screen_w - 8, 0,
                   color8(255, 190, 0, 255), 0)
elseif (cc2x.api or 0) < 4 then
    -- same, "needs a newer CC2X"
end
```

Do not ship CC2X (`dinput8.dll`) inside your mod, and do not ask players to run installers:
the one download link is the whole installation.

## 16. Rules that keep mods working

1. **Feature-test** `cc2x` and `cc2x.api` (Lua) / `api_version` and `size` (C). Never assume.
2. **No game addresses in mods.** Use the API; if something is missing, ask for it in CC2X.
3. **Ids, not pointers / tables** across ticks: vehicles disappear at any moment.
4. **Never move a vehicle that stands on its wheels.**
5. **Lua never blocks or errors**: wrap your code in `pcall`, keep per-frame work small, read the
   world a few times per second, not hundreds of times per frame.
6. **C: the right thread.** World API and patches on the server thread; Lua functions only
   read your own copies.
7. **One `library_custom_N` per mod**, documented. Do not replace whole screen scripts unless
   there is no other way.
8. **Single player / host only** for the world data and commands.
9. **Licence.** CC2X is GPL-3.0-or-later and `cc2x.h` is part of it: publish the source of C
   plugins under a GPL-compatible licence.

## 17. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| no `cc2x.log` in the game folder | CC2X is not loaded: `dinput8.dll` is not in the folder of `carrier_command.exe`, or Steam was not running (the game then exits before loading it) |
| `unsupported game build ... CC2X stays inactive` | the game was updated; wait for a CC2X update |
| `cc2x` is `nil` in Lua | CC2X not installed / inactive (see above) |
| `cc2x.api` is less than 4 | CC2X older than 0.5: download it again |
| your plugin is not in `cc2x.log` | the mod is not enabled (Mods menu, then restart), or the DLL is not in `content\cc2x\` |
| `Lua stack changed by a plugin callback; restored` | a plugin's `on_lua_state` callback left values on the Lua stack |
| `event ...: hook not installed` | another plugin patched the same place; see the lines before it |
| `cc2x.events()` returns `overflow` | the script did not call `events()` for a long time; call it every frame or refresh |
| `cc2x.vehicles()` is empty | not in a game, or you are a client in somebody else's multiplayer game |
| your Lua change has no effect | restart the game after changing a mod's files |
| Workshop upload "failed to copy content" | use SteamCMD (15.3) |
| Workshop rejects the preview | `thumbnail.png` over 1 MB |
