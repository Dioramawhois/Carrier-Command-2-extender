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
Build: `sdk/build.py` exposes `plugin(source, out)`; `native/build.py` of Island Airfields is a
complete example of a plugin project outside the SDK folder. Any 32-bit C compiler works.

Complete reference: `include/cc2x.h`. Working examples: `examples/hello_plugin/hello.c`,
`../native/plugin/island_airfields.c`.

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
