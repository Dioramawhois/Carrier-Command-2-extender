# Changelog

## 0.5.0 (2026-10-25)

- Lua mods get the whole world API (`docs/LUA_API.md`, `cc2x.api` 4): vehicles (type, position,
  orientation, velocity, docking, autopilot, waypoints), tiles and island airfields (runway,
  hangars, queues, parking bays), copied every server tick; `cc2x.events()` (session, vehicles
  added / removed, new waypoints, island spawns, answers to requests); commands for the next server
  tick (fields, waypoints, physics, brakes, gear, landing request, airfield queues, ray casts);
  rules for the game's own decisions (island controller per team, defence spawns, hangar slot).
  Game hooks are installed when a Lua mod first asks for them.
- `library_custom_0..9.lua`: documented as the way to add code to every screen without replacing
  one; example and test in `tests/lua_mod`.
- The snapshots of API 1 (`api->vehicles`, `api->tiles`) come from the same world copy, every
  tick; the core keeps no game offsets of its own any more.

## 0.4.0 (2026-10-25)

- API 3, the world API ([docs/WORLD_API.md](docs/WORLD_API.md)): vehicles, tiles, island
  controllers, airfields and waypoints through named fields and lists; vehicle physics (body
  position, orientation, velocity) and the game's ray casts; game functions (brakes, gear, landing
  request, waypoint sync, airfield coordinates); game events (new waypoints, airfield tick, hangar
  choice, island controller for a team, island defence spawn) whose hooks are installed by their
  first subscriber. Plugins need no game addresses: Island Airfields uses only this API.
- `tests/selftest`: plugin that checks the world API in a running game.

## 0.3.1 (2026-10-25)

- Plugins of a mod live in `<mod>\content\cc2x\*.dll`: the game's Workshop upload takes only
  `mod.xml`, `thumbnail.png` and `content`, so a `cc2x` folder in the mod root never reached Steam.
  `<mod>\cc2x\*.dll` still works for local development. Needed by Island Airfields 1.0.

## 0.3.0 (2026-10-25)

- Plugins come with their mods: CC2X reads the game's list of enabled mods (`mods.xml`, local and
  Steam Workshop mods alike) and loads `<mod>\cc2x\*.dll` of each. Disabling a mod in the game's mod
  menu disables its plugin. A plugin file name is loaded once; an enabled mod wins over an old copy
  in the game folder (logged).
- API 2: `plugin_dir()` and `config_dir` (`%APPDATA%\Carrier Command 2\cc2x`).
- Release = the bare `dinput8.dll` plus readme and SHA-256. Licence GPL-3.0-or-later.

## 0.2.0

- One `dinput8.dll` in the game folder: build check, transactional patching, server and session
  events, world snapshots, `cc2x` table in the game's Lua, plugins `cc2x_*.dll` from the game folder.
