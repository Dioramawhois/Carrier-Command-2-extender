# ADR-0001: Additive shared carrier and settings extensions

Status: implemented in the 0.9.0 development candidate; game acceptance pending.
Date: 2026-11-05.

Brake v15's Lua policy can already brake/hold vehicles, but independent Lua
cannot issue the carrier's dock request, handle a parked aircraft through the
stock landing manager or persist its own settings. The author's research at
`a40d1572eabd6d9710e6d64aae431824ffc98790` identifies checked native primitives.

Expose separate named C interfaces and equivalent optional Lua functions over
one backend. Do not enlarge API 8/framework 1 structures or bump Lua API 2 merely
to add optional functions. Require explicit capability discovery. No mod policy,
native gameplay DLL, stock screen or automatic activation belongs in the SDK.

Dock work uses copied IDs, next-tick dispatch, a bounded owner queue and explicit
completion. Recovery hooks install lazily as one byte-checked transaction, retain
existing plugin ownership, preserve registers, constrain one parked vehicle and
count budgets inside the hooks. One continuous recall cannot be renewed through
repeated calls, disarm or module failure. Session teardown invalidates all work.

Provide owner-scoped typed settings rather than general filesystem access.
Validated finite values persist atomically, bounded by byte/key/rate limits;
unknown paths/reparse points are refused. Isolate case-sensitive identities using
hex-encoded owner filenames. Mods supply their UI; the SDK chooses no defaults.
Keep ModLoader's existing native INI defaults and binary review unchanged.

Retain earlier ABI/sandbox/screen behavior with the complete regression suite;
add paired C/actual-Lua fixtures and static executable checks. Offline or static
success cannot substitute for fresh gameplay, host/client, dedicated or macOS
acceptance. See CARRIER_RECOVERY.md and MODULE_SETTINGS.md for exact contracts.
