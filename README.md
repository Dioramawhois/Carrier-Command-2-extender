# CC2X SDK - Carrier Command 2 extender

CC2X is a shared framework for **C/C++ plugins and Lua mods**. Install one
`dinput8.dll` beside `carrier_command.exe`; gameplay mods remain separate,
optional packages. The SDK itself adds no aircraft, flight modes, brakes,
button layouts or island gameplay rules.

## Development preview being prepared: 0.10.0-dev

Adds optional [carrier motion and team contacts](docs/CARRIER_MOTION.md) through
one guarded C/Lua backend. It uses native waypoint steering, owner/session leases,
manual helm/throttle priority and a normalized forward-power cap. Local single-peer
sessions only; remote play is refused. Route UX and defensive policy belong to
separate mods. The owner accepted Carrier: Autopilot Waypoints alpha.4 in the
ordinary Steam installation with Revolution through ModLoader, after the live
helm factors, continuous turning power and route-repeat corrections. The exact
tested DLL is preserved, including its original build record; every compiled
input is verified against the accompanying committed source snapshot.
Separate combat/countermeasure expenditure, exhaustive save/load, multiplayer
and clean-machine acceptance remain unverified.

## Previous development preview: 0.9.0-dev

Adds optional [carrier docking and deck recovery](docs/CARRIER_RECOVERY.md)
and [persistent module settings](docs/MODULE_SETTINGS.md), shared by C and Lua.
Also adds [parked-vehicle equipment changes](docs/VEHICLE_EQUIPMENT.md), used
by the separate Island Airfields 2.2 addon. The exact tested runtime is released
without changing its embedded version or native code; this is a prerelease.
The published 0.8.1 release does not contain these interfaces. Stable C API 8,
framework API 1, Lua API 2 and existing screen extensions remain supported.
The owner tested Island Airfields equipment changes and three-arrival save/reload
in an ordinary Windows host game. New native interfaces are not a blanket claim
of full Brake, remote-client or dedicated-server acceptance. The optional CPU
tile-site preview currently refuses the tested world's definitions; distant
unstreamed map coordinates are not a supported feature of this preview.
Build and packaging commands do not install or publish the runtime.

## Previous client beta: 0.8.1-client-beta.1

Unifies guarded incoming waypoint origin and bounded session-safe dispatch
from 0.8.0 with the package-owned screen extensions from 0.7.1.
One current runtime supports both capabilities. The SDK supplies the extension
mechanism; enabled mods supply their own UI policy. See
[screen extensions](docs/SCREEN_EXTENSIONS.md).
Optional hash-pinned source edits let addons integrate into reviewed screens
without bundling or rewriting another mod's files. Unknown sources are skipped.
[Waypoint origin](docs/ORDER_ORIGIN.md), [private runtime fixtures](docs/RUNTIME_SANDBOX.md)
and [multiplayer validation scope](MULTIPLAYER.md) describe the evidence.

This release provides the stable **C API 8** prefix, the new **framework C API
1**, and **independent Lua API 2**. Both languages use the same checked engine
operations for supported capabilities. The original x86 C prefix remains 296
bytes; new functions use a named extension rather than an incompatible struct
cast. [C/Lua capability map and examples](docs/C_LUA_PARITY.md).

- **Independent Lua modules:** `require('cc2')`, checked world selectors, engine
  events, early read-only rules, named controls, throttle access, requests and
  published states, metadata, custom vehicle/UI catalogs and next-tick tasks.
- **Native C/C++ plugins:** the same engine mechanisms, plus registration of
  functions in independent Lua VMs. Native plugins are separately packaged;
  no gameplay plugin is compiled into this shared runtime.
- **Game-owned screen Lua:** the existing `cc2x` bridge and module request/state
  channel remain available. Independent and game-owned VM handles are distinct.

Gameplay mods are separate releases. Island Airfields now has a separate Lua
implementation; the full Brake v15 port remains separate work. Remote multiplayer, dedicated servers,
full flight/landing, changed-catalog saves, clean-machine acceptance and native
macOS support remain unverified. Offline tests are not gameplay acceptance.
See the exact release's `RELEASE.json` and test evidence for its verified scope.

## Install the runtime

Download [the matching development preview](https://github.com/Dioramawhois/Carrier-Command-2-extender/releases/tag/0.10.0-dev)
or subscribe to the existing [SDK Workshop item](https://steamcommunity.com/sharedfiles/filedetails/?id=3833014670).
A subscription downloads the files; it does not copy the bootstrap into the
game installation.

1. Close the game.
2. Copy `dinput8.dll` from the release, or the Workshop item's `content` folder,
   into the folder containing `carrier_command.exe`.
3. Preserve a backup of an older known CC2X DLL. Do not overwrite an unrelated
   loader without checking compatibility.
4. Install and enable your gameplay mods separately, then start the game.

[Detailed player instructions](docs/README_PLAYERS.txt) include the exact Steam
folder. Automatic installation requires a ModLoader build that explicitly
reviews this exact 0.10.0-dev runtime and its API capabilities. Existing ModLoader
releases pinned to older binaries require a matching update. Use the manual
route until that update is available. Do not bypass its DLL checks.
After a manual SDK update, copy the new matching DLL again while the game is
closed: Steam does not replace the game-folder copy. A compatible ModLoader is
recommended for reviewed installation and upgrades with backups.

## Write a mod

- [Lua framework guide](docs/FRAMEWORK_LUA_MODS.md) and [independent modules](docs/INDEPENDENT_LUA.md).
- [C/Lua operation map](docs/C_LUA_PARITY.md), [C plugins](docs/PLUGINS.md) and [world API](docs/WORLD_API.md).
- [Full mod-author guide](docs/GUIDE.md) and [game-owned Lua API](docs/LUA_API.md).
- [C11 example](examples/framework_c_plugin/framework.c) and [C++17 example](examples/framework_cpp_plugin/framework.cpp).

Gameplay policy belongs in the mod. Engine research becomes a checked reusable
primitive exposed to both C and Lua. A native implementation of an entire mod
with a Lua enable switch is not a replacement for a public Lua engine API.

## Build from source

Use Python 3.12+ and the hash-pinned Zig 0.15.2 Windows toolchain documented in
[the build guide](docs/GUIDE.md#111-tools). No Visual Studio is required.

```text
python build.py --zig PATH-TO-ZIG --test
python -m unittest discover -s tests -p "test_*.py"
```

The result is one `dist/dinput8.dll` with hashed build metadata. The native
fixtures execute real Lua against synthetic engine objects; Python tests check
packaging, separation and distribution. C/C++ plugins can use the external
SDK's `build.py` `plugin(source, output)` helper. Lua gameplay packages use
`tools/package_lua_mod.py` without a C compiler.

Publication is a separate operation. `tools/package_release.py` requires a
clean source commit, matching passing native evidence, unchanged input hashes
and the pinned provider-source archive. It stages runtime/source ZIPs, all
linked-component notices and checksums; it never publishes or installs them.
Release assets identify the exact compiled commit. Generated inputs are
included in the source asset and are regenerated by the build scripts.

## Source layout

| Path | Purpose |
| --- | --- |
| `include/cc2x.h` | Stable native C API prefix |
| `include/cc2x_framework.h` | Named C/Lua framework extension |
| `include/cc2x_session.h` | Legacy named session-safe queue extension |
| `sdk/cc2.lua` | Independent Lua facade |
| `src/cc2x.c`, `src/world.c`, `src/lua_world.c` | Proxy, verified game access and stock Lua bridge |
| `src/runtime` | Isolated VMs, lifecycle, catalogs, mailboxes and tasks |
| `src/api` | Shared checked engine mechanisms |
| `tests` | Offline regression fixtures; not live gameplay proof |
| `distribution` | Historical release records |

The previous [0.6 client beta](https://github.com/Dioramawhois/Carrier-Command-2-extender/releases/tag/0.6.0-client-beta.1)
is preserved unchanged. Its acceptance and binary hashes are historical evidence,
not evidence for a different 0.7 DLL.

## How it works (short)

- Loaded by the game itself: GLFW loads `dinput8.dll` for joysticks, the game folder comes first.
  All five dinput8 exports forward to the system DLL.
- `DllMain` checks the exe (PE timestamp + image size; `PROFILES` in `src/cc2x.c`), installs the
  core hooks and loads the plugins - before any game logic runs, so patching is race free.
  Unknown build -> log line, nothing patched, the game runs vanilla.
- Core hooks: the logic server's vtable (init / tick / end -> session and tick events) and the place
  where the game creates a Lua environment (`0x004465ec`, right after the standard libraries are
  opened) -> the `cc2x` table is put into every game Lua state.
- The Lua bridge uses the game's own Lua 5.3 internals (`index2addr`, `auxsetstr`, `luaV_finishset`,
  `luaS_new`, `luaH_new`, `luaC_step`; their first bytes are verified at startup) plus direct stack
  writes for numbers and C functions. These functions use a register calling convention
  (ecx, edx, rest on the stack, caller cleans) - see the `x_*` thunks.
- Patching is transactional: every change checks the original bytes, overlapping patches of two
  plugins are refused, and a failed commit restores everything.

## Limits / next steps

- [Isolated multiplayer development](MULTIPLAYER.md) records the session-safety
  changes, current host audit and the remaining network/dedicated gates.
- Known game builds only (1.5.18 Windows: the original exe and the Steam rebuild of 2026-10-28). A new
  build needs a new `PROFILES` entry (core addresses) and a `LAYOUTS` entry in `src/world.c`.
- No multiplayer transport: Lua functions see the local server only.
- Plugins cannot be unloaded; patches stay for the process lifetime.
- Possible additions when a mod needs them: more world data (teams, inventories, island capture),
  more game events, a multiplayer transport.

## License

CC2X is free software under the GNU General Public License v3.0 or later ([LICENSE](LICENSE)).
`include/cc2x.h` is part of it: a plugin built against this API is a work based on CC2X, so a plugin
you distribute must be released under GPL-3.0-or-later (or a GPL-compatible licence) with its
source code. Carrier Command 2 itself is not part of this project and keeps its own licence.
