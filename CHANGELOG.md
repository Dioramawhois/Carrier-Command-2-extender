# Changelog

## 0.6.0-dev (unreleased)

- Added generic developer consumer packaging, preserving native provenance and
  resource bytes, syntax-only Lua/XML validation, licensing and atomic new output.
  Separate projects now prepare Hold UI, original Modes UI, Brake and aircraft
  packages; config activation/native/game acceptance remain distinct gates.
  Eight packaging regressions cover guarded sources, paths, PE data, unchanged
  existing destinations, required Lua checker and data-only consumers.

- Validate every independent module entry against game-owned stock scripts,
  including later scripts after a valid first entry. Added a failing regression
  before fixing discovery; the complete integrated SDK suite passes afterward.

- Integrated the authorized team Native Bridge into this runtime: independent
  Lua host, generic definition/UI/resource registries, cc2 facade and one C core.
  Canonical API 8 preserves the API 4 prefix; named sessions do not occupy its tail.
- Default build produces the whole runtime with pinned Zig 0.15.2. No VS install
  is needed; native SEH remains in a small separately compiled C boundary.
  Build metadata records compiler, source/generated inputs, revision and hashes.
- Added bounded ordered module-container discovery with private roots. Invalid
  bundles fail atomically, duplicate owner IDs reject all colliding modules.
- Aircraft-specific branches are removed from production; research activation
  and incompatibility-policy bypass are test references, not linked features.
  Team authorization, source snapshot hashes, GPL and Lua MIT notices are retained.
- 18 offline SDK tests and read-only current-game profile checks passed. These
  are not fresh gameplay/multiplayer/dedicated acceptance or native trust approval.

- Session command isolation is exposed through the separately named
  `cc2x_query_session_v1` extension, validated by interface ID, version and size.
  The stable API 4 prefix is preserved. The earlier draft's appended API 5 slots
  were removed after finding that Native Bridge uses API 5-8 for different fields.
- Missing/stale local sessions reject commands; ending/changing a session drops
  pending work. Legacy server_post also refuses calls with no local server.
- Added address-free queue and extension-ABI tests. They check FIFO/capacity,
  session/address reuse, absent extension, identity/version/size and callbacks.
- Offline tests and Windows x86 builds pass. No RPC, dedicated profile/bootstrap
  or real multiplayer acceptance is claimed. Same runtime/library identity.

## 0.5.1 (2026-10-29)

- Supports the Steam update of Carrier Command 2 from 2026-10-28. The game still says 1.5.18, but
  the exe was rebuilt and the game's code moved; CC2X 0.5.0 stayed inactive on it ("unsupported
  game build"). New build id `1.5.18-win32-r2`; the previous 1.5.18 exe keeps working too.
  Mods need no update.

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
