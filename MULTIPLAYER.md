# Multiplayer work in the next CC2X version

This updates the existing CC2X SDK (same runtime/library ID), not a new mod.
Named session-extension v1 command lifetime isolation is implemented and offline-tested. It does not
add RPC, replicate arbitrary plugin state or certify dedicated-server support.

Current client profiles remain the known 1.5.18 Windows x86 builds. The locally
inspected dedicated_server.exe is PE32 x86, timestamp 0x6ae22909, image size
0x653000. It imports no dinput8 or GLFW, so the current proxy does not bootstrap it.
Client offsets are not a dedicated profile. No vanilla executable is modified.

Remaining gates: verify dedicated server/world layouts and bootstrap; test host
authority, client waypoint delivery and replicated state; disconnect/reconnect,
session replacement, save/reload and permissions on ordinary and dedicated hosts.
The author's single computer cannot certify a second licensed player session.
Native builds and queue tests do not constitute multiplayer acceptance.

## Isolated development audit (2026-11-03)

Worktree `cc2x-network`, branch `codex/multiplayer-session-safety`, starts at
`8bafe71a9d43d8ed31e9eab1955d90d85f91b157` (framework 0.7 client beta).
Its consumer worktree is `island-airfields-network`, branch
`codex/multiplayer-order-safety`. ModLoader and the other SDK/mod worktrees are
outside this change. Candidate binaries stay in these worktrees; no installed
runtime, Workshop mod, game configuration or executable is written.

Completed here: bounded actual-core queue dispatch, epoch isolation through tick
subscribers, enabled assertions in optimized regressions and cache invalidation
for fixtures including production sources. See the session extension contract
and CHANGELOG. The runtime is now 0.8.0-client-beta.1; API 8 keeps its existing prefix. The optional
named order-origin API is additive. Published release hashes remain unchanged.

Fresh read-only installed PE inspection found:

| Host | SHA-256 | PE timestamp / image size |
| --- | --- | --- |
| `carrier_command.exe` | `461e12623bacb7de29ca27b4614425c87e8df2c8327a5e958a957c850effe42a` | `0x6ae228f3` / `0xaf7000` |
| `dedicated_server.exe` | `718fabf6623c65c38593df1bcb4362a61cc96d967c80f18a5de5ea74a6217abb` | `0x6ae22909` / `0x653000` |

Both are PE32 x86. The dedicated image imports only KERNEL32, SHELL32,
steam_api and WS2_32. Import inspection does not prove every possible dynamic
load path. The present SDK is a guarded client profile using the game's GLFW
dinput bootstrap; it has no reviewed dedicated bootstrap or world layout.
The client profile's existing static signature verifier passed against the
installed executable. The installed executables were not run or modified for this audit. Subsequent
private copied-client sessions are described below.

## Implementation order and acceptance

1. **Local session safety:** the fixes above and the existing C/Lua paired
   fixtures. These are prerequisites for multiplayer, not a network result.
2. **Ordinary host and remote client:** prove the real command receive path,
   sender/team permissions, authoritative host role and host-to-client state
   replication. Provide any missing operations as generic shared C/Lua primitives.
   A local logic-server pointer or local mailbox is not a remote-host handshake.
3. **Protocol:** negotiate runtime/module capability versions and selected
   catalog identity. Bind acknowledgements and replicated snapshots to a verified
   network session and request identity; reject stale/replayed requests. Local
   generation numbers alone cannot identify the same session across processes.
4. **Island Airfields Lua policy:** retain the native implementation as a
   regression reference while moving gameplay into its separate Lua package.
   Flight/runway/hangar policy belongs in the mod; transport, checked engine
   operations and thread/session boundaries belong in the SDK, available to C
   and Lua. A native plugin with a Lua enable flag is not that migration.
5. **Dedicated:** independently review its bootstrap, lifecycle/world layout and
   exact-byte guards. Do not copy client offsets or patch the vanilla executable.
6. **Acceptance:** ordinary host + a separate licensed remote client, then
   dedicated + clients. Cover accepted/rejected landing, authority, flight/body
   replication, cancellation, runway contention, full queues, disconnect,
   reconnect, late join, host/session replacement and save/reload with matching
   catalogs. Record fresh host/client logs and exact binary/source hashes.

Current offline evidence is `build/unified/test-results.json` (27 native
executables, assertions enabled) and 32 Python regressions. Installed PE evidence
is `build/game-host-audit.json`. These counts describe this development snapshot.

## Guarded incoming order origin

[ORDER_ORIGIN.md](docs/ORDER_ORIGIN.md) specifies the optional named C table and
matching `cc2.orders` Lua interface. The client receive hook provides a copied,
scoped vehicle/peer/core-epoch record only while processing a verified incoming
order. The original handler broadcasts after the hook. Nested contexts, absent
sender, malformed rosters and session replacement fail closed. Peer IDs are
engine peer IDs; they are not Steam IDs or authenticated team/admin claims.

Private live read-only inspection corrected the receive roster: the manager is
at server + 0xc48, but its actual peer deque is at manager + 0x20. The initial
manager deque is empty. Exact instruction guards and decoder fixtures cover this
layout. No client layout is presumed valid for the dedicated executable.

## Private client validation

The separate Island Airfields Lua policy is implemented and runs through the
real Host in synthetic regression tests. A private byte-identical client copy
has also loaded it in actual sessions with zero module errors, including session
replacement. The user reported successful ordinary-host gameplay and the follow-up map-marker
check on 2026-11-04. Those observations used the preceding development DLL;
release metadata identifies exact-binary checks separately. No second
licensed client is available, so remote receipt, body replication and host
capability handshake are unverified. Dedicated bootstrap remains unimplemented.

See [RUNTIME_SANDBOX.md](docs/RUNTIME_SANDBOX.md) for the test-only profile isolation
adapter, input provenance and limitations. Test drivers and private screen edits
are not shipped in the SDK or used as proof for an untouched player package.
Local mailboxes and local generations are never advertised as network transport
or network session identities.
