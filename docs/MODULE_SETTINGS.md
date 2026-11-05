# Persistent per-module settings (0.9.0 development)

Optional capability `module-settings-v1`; independent Lua stays API 2.
Published 0.8.1 does not have this capability. Settings do not enable any
gameplay feature by default, and do not modify a mod's Workshop files.

```lua
local cc2 = require('cc2')
local brake = cc2.settings.get('brake', 0.75, 0, 1)
local hold_idle = cc2.settings.get('hold_idle', false)
-- On a user choice, not on every update:
local ok, reason = cc2.settings.set('hold_idle', true)
-- Optional bounded refresh after an external settings change:
local ok, reason = cc2.settings.reload()
```

`get(key, default[, minimum, maximum])` reads a cached finite number or boolean.
A missing key or different stored type uses the supplied default. Numeric values
are clamped to finite bounds; the default must already be in range. Numeric
0/1 can be read as booleans for INI interoperability. Invalid arguments return
nil plus a reason. The fallback facade on an older runtime returns the default
and `unavailable`.

`set` accepts only a finite number or boolean and persists immediately. Check
its boolean result and reason. `reload` swaps the cache only after complete
validation; malformed files keep the prior cache. Removing the file and then
reloading restores defaults. Neither operation may exceed eight attempts per
owner per wall-clock second. Cached reads require no per-update disk access.
Calls require the runtime's active server context; early construction-time
rule VMs have no settings interface. User-facing settings controls belong in
the mod's UI and can use the existing request/state channel.

Storage is the SDK config directory, with deterministic filenames
`settings-<lowercase hex bytes of owner>.ini`. Lua owner is `lua.<module_id>`;
native owner is the explicit `native.<name>`. Hex encoding keeps case-sensitive
identities separate on Windows. Mod updates and Lua reloads do not replace these
files. No filename, directory or file handle is accepted from Lua.

Files contain flat `key=value` entries, optionally under a single `[owner]`
section; only finite numbers and `true`/`false` are accepted. Names use ASCII
letters/digits/underscore/hyphen with nonempty dot components, at most 96 bytes.
Reserved Windows device names are refused. Maximum 128 keys and 64 KiB; duplicate
keys, other sections, NULs, strings, NaN and infinity are rejected. UTF-8 BOM,
blank lines and full-line #/semicolon comments are accepted. Saving normalizes
the file to sorted flat entries; comments and formatting are not retained.

Saving first validates the existing contents, preserving unrelated valid keys.
Malformed, oversized or unsafe files are never silently overwritten. The write
is staged with CREATE_NEW, flushed, and atomically renamed by its exact handle.
Failures retain the previous target and cached values. Ancestor directories are
locked against rename/delete, and reparse points or multiply linked leaf files
are refused. No Lua `io`, `os`, native loader or arbitrary file access is added.
Concurrent external editors should finish their save before a mod writes;
the interface provides atomic file replacement, not multi-process transactions.

C consumers include `cc2x_settings.h` and resolve
`cc2x_settings_from_core(api)`, then use `number`, `boolean`, `set_number`,
`set_boolean` and `reload` with their explicit `native.` owner. Null discovery
means unavailable. It is the same store and validation used by Lua, without
changing either existing C structure. Native plugins already have OS access;
the owner namespace is an API convention, not native-code isolation.

ModLoader ADR-0018's reviewed native INI defaults remain a separate mechanism.
This interface neither imports legacy frein/modes INI nor changes ModLoader's
runtime hash allowlist. A future GUI integration must review the exact SDK
binary and its setting keys/defaults. Persistence tests use temporary directories;
no installed settings or game files are changed during SDK verification.
