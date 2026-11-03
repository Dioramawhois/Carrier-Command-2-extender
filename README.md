# CC2X - Carrier Command 2 extender

One file, `dinput8.dll`, put once next to `carrier_command.exe`. It gives

- **Lua (Workshop) mods** a global `cc2x` table in the game's own Lua: the live world (vehicles,
  islands, airfields), game events, commands and functions added by plugins - no C needed - see
  [docs/LUA_API.md](docs/LUA_API.md);
- **native plugins** a C API: the live game world (vehicles, islands, airfields, physics, game
  events) without game addresses - see [docs/WORLD_API.md](docs/WORLD_API.md) - plus safe patching,
  server events and registering Lua functions - see [docs/PLUGINS.md](docs/PLUGINS.md). A plugin ships inside its mod (`<mod>\content\cc2x\*.dll`); CC2X loads
  the plugins of the mods enabled in the game's mod menu, Steam Workshop items included.

**0.6.0-client-beta.1 (2026-11-03):** this release contains the same API 8 runtime
as the [CC2X SDK Workshop item](https://steamcommunity.com/sharedfiles/filedetails/?id=3833014670).
The DLL still reports `0.6.0-dev`; its SHA-256 is
`ce4fc1c883c70af6c40f5431003d17584e9b2becb502ac840d22f73be9d7a7f3`,
built from source commit `2eecbd93c3555fed9a2b3f25e5b9c91abb55196f`.

The shared runtime includes the team's Native Bridge independent
Lua VMs, `require('cc2')` facade, generic vehicle/UI/resource registries and bounded
ordered module bundles. Gameplay scenarios stay in separate mods. The C API uses
the team's API 8 layout with the original API 4 prefix preserved; session commands
use a checked named extension. See [integration status](docs/UNIFIED_SDK_PLAN.md)
and [module packaging](docs/MODULE_BUNDLES.md).

On November 3, the reviewed Windows 1.5.18-r2 client passed single-player checks
with AI Bridge, Island Airfields v46, Hold, Modes, Brake and separate aircraft:
combined native/Lua loading, custom aircraft registration and hangar creation,
save/reload of the unchanged catalog, and creation again after reload. Remote
multiplayer, dedicated-server support, changed-catalog saves, full flight/island
landing and native macOS support remain unverified.

**Mod makers:** start with [docs/GUIDE.md](docs/GUIDE.md) - the complete guide from the first Lua
mod to a published Workshop item.

**Players:** see the [client beta release](https://github.com/Dioramawhois/Carrier-Command-2-extender/releases/tag/0.6.0-client-beta.1)
and [installation instructions](docs/README_PLAYERS.txt). Recommended automatic
installation requires the matching CC2 ModLoader 0.4.0 beta 2, currently a team
release candidate; the older public beta 1 cannot use this runtime. Alternatively,
copy [dinput8.dll](https://github.com/Dioramawhois/Carrier-Command-2-extender/releases/download/0.6.0-client-beta.1/dinput8.dll)
(or the Workshop item's `content/dinput8.dll`) next to `carrier_command.exe`.
A Workshop subscription downloads the SDK but does not install the loader itself.
Gameplay mods are installed separately.

## Layout

| | |
|---|---|
| `include/cc2x.h` | the whole plugin API (one header) |
| `src/cc2x.c` | the extender: dinput8 proxy, build check, patch engine, events, Lua bridge, snapshots, plugin loader |
| `src/lua_world.c` | the world for Lua: per-tick copy, events, command queue, rules |
| `src/world.c` | the world API: where the game keeps its objects in each supported build, physics, game events |
| `tests/lua_mod` | Lua mod that checks the Lua world API (`library_custom_9.lua`) |
| `tests/selftest` | plugin that checks the world API in a running game (PASS / FAIL in `cc2x.log`) |
| `examples/hello_plugin` | minimal plugin |
| `build.py` | build with the bundled Zig toolchain (`.deps`, a C compiler for 32-bit Windows) |

| `examples/lua_air_traffic` | example Lua mod (`library_custom_7.lua`) |

`python build.py --zig PATH` -> the one integrated `dist/dinput8.dll` and hashed
build metadata. `--test` runs the 19 offline SDK checks; `--examples` also builds
hello/selftest plugins. `--release` stages a developer candidate with readme,
GPL/Lua notices and SHA-256; it does not certify game acceptance or publish it.
Zig 0.15.2 builds the C/C++ core, static Lua and a small isolated SEH boundary
without Visual Studio. The compiler setup is in
[docs/GUIDE.md](docs/GUIDE.md#111-tools).

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
