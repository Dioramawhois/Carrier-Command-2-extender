# Session extension v1: fork-safe contract

The inspected Native Bridge source snapshot (2026-11-02) identifies its cc2x_api
as version 8. Its API 5 begins with active_mod_count, active_mod_path, patch_bytes;
API 6 adds native operations and API 8 adds lua_add_global. Our previous draft
had placed session functions at the same API 5 offsets. Passing a min-version
and size check cannot distinguish these layouts and may call the wrong function.

The replacement preserves the original API 4 prefix exactly and exposes a named
optional C extension from the existing runtime, not a second loader or mod.
Export: cc2x_query_session_v1(uint32_t version, uint32_t caller_size).
For version 1 and the exact v1 struct size it returns a process-lifetime immutable
table with version, size, interface_id 0x4343325853455331, generation and post.
Unknown version/size returns NULL. No allocated object is transferred.

Consumers resolve the export from the module owning a known base-API callback,
then validate the returned table before reading callbacks. Unknown forks without
this extension remain inactive. A larger numeric cc2x_api version is insufficient.
The unified runtime now adopts Native Bridge's canonical API 8 tail, while this
extension remains separately named. CC2X_API_V4_SIZE is the prefix boundary, not
sizeof the expanded header. Existing API 4 consumers can remain prefix-only;
earlier draft binaries with session fields appended as API 5 must be rebuilt.
Existing native hash/profile review gates still apply.

## Dispatch and session replacement

Both legacy `server_post` and named session `post` use the same bounded FIFO.
At dispatch entry the runtime captures its epoch and current queue length. Only
that prefix runs in this tick. Work enqueued by those callbacks waits until a
later dispatch; capacity exhaustion returns 0 and does not discard old work.
Ending or replacing the session invalidates the prefix and prevents later tick
subscribers or accelerated sub-ticks from running for that obsolete session.
Pointer equality does not establish session identity: epochs must match too.
Callbacks execute outside the queue lock and retain ownership of their user data.

This fixes the dispatch behavior without changing the C prefix, extension table,
Lua API, IDs or remote transport. The framework C/Lua next-tick task queue remains
its existing separate implementation. Local epochs are process-local counters;
they must never be treated as shared network session IDs or peer authentication.

Tests run before any game test: stable API prefix static assertions, absent
query, wrong interface ID/version/size, missing callbacks, queue/session reuse,
native DLL compilation and read-only PE export inspection. These are offline
checks. Do not install a development runtime over an unknown existing DLL.

The actual-core fixture also checks nested posts, full-capacity FIFO refill,
same-address session replacement and replacement from a tick subscriber with
accelerated ticks enabled. Optimized test builds explicitly undefine `NDEBUG`.
Earlier assert-based test results must be reevaluated with assertions enabled;
an old PASS line alone does not establish that those checks executed.
