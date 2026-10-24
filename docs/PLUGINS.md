# Writing a CC2X plugin

A plugin is a 32-bit Windows DLL `cc2x_<name>.dll` with one export:

```c
#include "cc2x.h"

CC2X_EXPORT int cc2x_plugin_load(const cc2x_api *api)
{
    if (api->api_version < CC2X_API_VERSION) return 0;
    /* patch, subscribe to events, register Lua functions ... */
    return 1;
}
```

It is called once at game start **inside the loader lock** (from `DllMain` of `dinput8.dll`):
no threads, no waiting, no `LoadLibrary`. Do the real work in the event callbacks.
Build: `build.py` exposes `plugin(source, out)`; `native/build.py` of Island Airfields is a
complete example of a plugin project in its own repository. Any 32-bit C compiler works.

Complete reference: `include/cc2x.h`. Working examples: `examples/hello_plugin/hello.c`,
the Island Airfields repository.

## Shipping a plugin: inside your mod

Put the DLL into your mod's `content` folder, in a `cc2x` subfolder, and publish the mod as usual
(Steam Workshop or a local mod). It has to be inside `content`: the game's Workshop upload takes
only `mod.xml`, `thumbnail.png` and the `content` folder.

```
My Mod/
    mod.xml
    thumbnail.png
    content/...              your Lua, models, ... (whatever the game loads)
    content/cc2x/cc2x_mymod.dll   your plugin
```

CC2X 0.3+ reads the game's list of enabled mods (`%APPDATA%\Carrier Command 2\mods.xml`) at start
and loads `content\cc2x\*.dll` of every enabled mod (`<mod>\cc2x\*.dll` works too, for local
development). Players install nothing but CC2X itself; disabling the
mod in the game's mod menu disables the plugin (after a restart). Plugins with the same file name are
loaded once: an enabled mod wins over a copy in the game folder (the game folder and
`<game>\cc2x\plugins` still work, for development).

Settings belong into `api->config_dir` (`%APPDATA%\Carrier Command 2\cc2x`): Steam replaces the mod
folder on every update. `api->plugin_dir()` is the plugin's own folder during `cc2x_plugin_load`
(for data files next to it). Both are API 2: check `api->api_version >= 2`.

## Licence

CC2X is GPL-3.0-or-later and `cc2x.h` is part of it: publish your plugin's source code under a
GPL-compatible licence.

## Addresses

All addresses are image VAs for base `0x00400000` (as Ghidra shows them). `api->va(x)` rebases.
Check `api->game_build` and refuse to load on a build you do not know.

## Patching

```c
api->patch_begin("my_plugin");
api->patch_call(site, len, original_bytes, my_fn, &original_fn);  // call -> my_fn (+ nops)
api->patch_mid(site, len, original_bytes, my_hook, user);         // registers in, registers out
api->patch_ptr(vtable_slot, expected_target, my_fn, &original);   // vtable / pointer
if (!api->patch_commit()) return 0;                               // all or nothing
```

- `patch_call`: the replaced bytes become `call my_fn` + `nop`s. If they were a `call rel32`,
  `original_fn` is its target. Use `__attribute__((thiscall))` etc. to match the game's convention.
- `patch_mid`: `my_hook(cc2x_cpu *cpu, void *user)` runs with the registers of the hooked code;
  change them, or set `cpu->jump_to = api->va(addr)` to continue elsewhere instead of the displaced
  instructions. `CC2X_STACK(cpu)[i]` reads the stack. The displaced bytes must not contain relative
  jumps (except a leading call/jmp rel32, which is relocated).
- Every function verifies the original bytes; overlapping patches of two plugins are refused.

## Events and threads

| | thread |
|---|---|
| `on_server_tick(fn, user)` | logic server, after the game's tick |
| `on_session(fn, user)` | logic server; `started` 1/0 when a game is loaded / left |
| `on_lua_state(fn, user)` | thread creating Lua screens; after the `cc2x` table exists |
| Lua functions you register | the Lua screens' thread |

Lua functions must not touch server objects directly (another thread). Copy what Lua needs on the
server tick under your own lock (see `refresh_table` / `lua_count` in Island Airfields), and send
actions to the server with `api->server_post(fn, user)` (runs at the next tick).

## Lua functions

```c
static int my_fn(cc2x_lua *L) {
    int64_t id;
    if (!api->lua_tointeger(L, 1, &id)) { api->lua_pushnil(L); return 1; }
    api->lua_newtable(L);
    api->lua_pushinteger(L, id); api->lua_setfield(L, "id");
    return 1;
}
api->lua_add("my_plugin", "get", my_fn);   // cc2x.my_plugin.get(id)
api->lua_feature("my_plugin");             // cc2x.has("my_plugin") == true
```

Never raise Lua errors from C (return `nil`, "reason"); use at most ~15 stack slots per call;
`lua_appendi` only appends new integer keys to a table you created.
