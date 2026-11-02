# Session extension v1: fork-safe contract

The inspected Native Bridge source snapshot (2026-11-02) identifies its cc2x_api
as version 8. Its API 5 begins with active_mod_count, active_mod_path, patch_bytes;
API 6 adds native operations and API 8 adds lua_add_global. Our previous draft
had placed session functions at the same API 5 offsets. Passing a min-version
and size check cannot distinguish these layouts and may call the wrong function.

The replacement preserves the original API 4 struct exactly and exposes a named
optional C extension from the existing runtime, not a second loader or mod.
Export: cc2x_query_session_v1(uint32_t version, uint32_t caller_size).
For version 1 and the exact v1 struct size it returns a process-lifetime immutable
table with version, size, interface_id 0x4343325853455331, generation and post.
Unknown version/size returns NULL. No allocated object is transferred.

Consumers resolve the export from the module owning a known base-API callback,
then validate the returned table before reading callbacks. Unknown forks without
this extension remain inactive. A larger numeric cc2x_api version is insufficient.
No Native Bridge implementation was copied; this does not claim compatibility
with its extra native features or game profiles. Existing binary review gates apply.

Tests run before any game test: stable API prefix static assertions, absent
query, wrong interface ID/version/size, missing callbacks, queue/session reuse,
native DLL compilation and read-only PE export inspection. These are offline
checks. Do not install a development runtime over an unknown existing DLL.
