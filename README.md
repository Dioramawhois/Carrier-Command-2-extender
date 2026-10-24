# CC2X - Carrier Command 2 extender

One file, `dinput8.dll`, put once next to `carrier_command.exe`. It gives

- **Lua (Workshop) mods** a global `cc2x` table in the game's own Lua: world data from the server
  and functions added by plugins - see [docs/LUA_API.md](docs/LUA_API.md);
- **native plugins** a small C API: safe patching, server events, registering Lua functions - see
  [docs/PLUGINS.md](docs/PLUGINS.md). A plugin ships inside its mod (`<mod>\cc2x\*.dll`); CC2X loads
  the plugins of the mods enabled in the game's mod menu, Steam Workshop items included.

**Players:** download [dinput8.dll](https://github.com/Dioramawhois/cc2x/releases/latest/download/dinput8.dll) and put it into the game folder - details in
[docs/README_PLAYERS.txt](docs/README_PLAYERS.txt).

## Layout

| | |
|---|---|
| `include/cc2x.h` | the whole plugin API (one header) |
| `src/cc2x.c` | the extender: dinput8 proxy, build check, patch engine, events, Lua bridge, snapshots, plugin loader |
| `examples/hello_plugin` | minimal plugin |
| `build.py` | build with the bundled Zig toolchain (`.deps`, a C compiler for 32-bit Windows) |

`python sdk/build.py` -> `sdk/dist/dinput8.dll`, `sdk/dist/cc2x_hello.dll`;
`--release` also packs `sdk/release/CC2X-<version>.zip`.

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

- One game build (1.5.18 Windows). A new build needs a new `PROFILES` entry (addresses + checks).
- No multiplayer transport: Lua functions see the local server only.
- Plugins cannot be unloaded; patches stay for the process lifetime.
- Possible additions when a mod needs them: more world data (teams, inventories), commands
  executed on the server, events for vehicle spawn/destroy and island capture.

## License

CC2X is free software under the GNU General Public License v3.0 or later ([LICENSE](LICENSE)).
`include/cc2x.h` is part of it: a plugin built against this API is a work based on CC2X, so a plugin
you distribute must be released under GPL-3.0-or-later (or a GPL-compatible licence) with its
source code. Carrier Command 2 itself is not part of this project and keeps its own licence.
