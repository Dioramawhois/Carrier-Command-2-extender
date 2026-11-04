# Changelog

## 0.8.0-client-beta.1 (2026-11-04)

- Runtime identifies itself as `0.8.0-client-beta.1`. Add the optional
  named C order-origin interface and matching public Lua operations over one
  scoped backend. Check the actual source peer roster, callback thread and core
  epoch; expose no guessed team/admin identity. Pin the r2 receive/broadcast
  windows and preserve the stable prefix and existing named interfaces.
- Add native and paired C/real-Lua regressions, including a manager whose first
  deque is empty: the receiver's peer lookup uses its second deque at +0x20.
- Add an explicitly test-only private-game staging/isolation probe. The game
  uses a Windows folder query rather than the APPDATA environment alone; no
  redirection or test gameplay is compiled into the production runtime.
- Bound the legacy server-post dispatcher to the FIFO prefix present when
  dispatch begins. Commands posted by a callback wait for another tick, so a
  repeating callback cannot starve tick subscribers. Preserve the originating
  epoch across dispatch, subscribers and accelerated ticks; replacement sessions
  cannot consume the preceding session's tick even when addresses are reused.
- Enable C/C++ assertions explicitly in optimized native regressions. Zig's
  optimized defaults previously compiled out `assert` checks. Invalidate cached
  test objects on production-source changes, including direct C/C++ inclusions.
- Add actual-core regressions for nested posts, full-queue FIFO refill, session
  replacement and subscriber/time-scale termination. This is offline evidence;
  no remote transport, host capability handshake or dedicated support is added.

## 0.7.0-client-beta.1 (2026-11-03)

- Preserve C/C++ plugin development alongside Lua. Add the named framework C
  extension over the same buttons, throttle, input, mailbox, asset catalog,
  metadata and next-tick task backends, with paired real-Lua/native fixtures.
  Add C11/C++17 examples and a capability/validation map. Offline evidence does
  not imply full gameplay acceptance or completion of the Brake v15 port.

- Keep gameplay behavior in separate Lua/content mods. Add generic synchronous
  engine callbacks, bounded per-module screen request/state mailboxes, checked
  wing throttle access, and staged named-button state transport to Lua.
- Refuse stale/session-less requests, unsupported profiles, invalid values,
  disabled controls and conflicting button batches. An empty module set issues
  no gameplay commands in the host regression fixture.
- Add native-free Lua/content packaging with source hashes and a separate
  `lua_api` package requirement. Candidate outputs can be built outside `dist`
  without replacing the published SDK DLL.
- Update the cc2mods source lock to the author's a40d157 revision. Preserve the
  prior extraction lock as history. New generic engine code retains upstream
  attribution; no named Hold/Modes/Brake policy is compiled into the framework.
- This is a framework client beta. Brake/Island Lua migrations, full feature
  acceptance, remote multiplayer and dedicated-server support remain separate
  work. This release does not bundle or silently enable those gameplay mods.


## 0.6.0-client-beta.1 (2026-11-03)

- Publishes the same Windows API 8 runtime as Steam Workshop SDK item 3833014670.
  The runtime continues to report `0.6.0-dev`; its exact compiled source is
  `2eecbd93c3555fed9a2b3f25e5b9c91abb55196f`, and the DLL SHA-256 is
  `ce4fc1c883c70af6c40f5431003d17584e9b2becb502ac840d22f73be9d7a7f3`.
  The corresponding native regression report records 19 passing checks.
- Documents automatic installation with the matching ModLoader 0.4.0 beta 2
  candidate and manual DLL installation. Workshop subscription alone does not
  install the bootstrap, and gameplay mods remain separate. Public ModLoader
  beta 1 does not support this runtime. This is a client beta; multiplayer,
  dedicated servers and the remaining gameplay/platform gates stay open.

- Native consumer packages preserve MinGW/Zig redistribution notices; a packaging
  regression prevents their omission. Linked C++ runtime notices are preserved.
- Real Windows 1.5.18 client acceptance with AI Bridge, Island Airfields v46,
  Hold, Modes, Brake and separate aircraft: concurrent plugin load, isolated Lua
  updates, F-16/F-15 spawn, save/reload of unchanged catalog and subsequent spawn.
  Original game state/saves restored. Flight/landing, changed-catalog saves,
  remote multiplayer and dedicated acceptance remain open.

- The production generic definition adapter now owns its loader/reset and guarded
  name-map resize hooks. They were previously called only by an excluded research
  probe, leaving custom types unready in the real game. A production-adapter fake
  engine regression reproduces the missing hooks and verifies profile bytes,
  all-or-nothing installation, relocated return address and arbitrary catalog size.
  No research activation or aircraft-specific runtime branch is reintroduced.

- Consumer packaging keeps the original lexical project boundary before Windows
  8.3 root canonicalization, fixing RUNNER~1 source paths without hiding DLL links.

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
