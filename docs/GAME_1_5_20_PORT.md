# Carrier Command 2 1.5.20 compatibility port

Target: Steam Windows build **26732430**, depot 1489631 manifest
`2750529091208403553`. The installed executable and an independent SteamCMD
download have the same SHA-256:

`1df609c8252fc0245decfab793e3b8e1884e553539a8831cbee67663b6725920`

PE32 x86, preferred base `0x400000`, timestamp `0x6afc5c17`, image size
`0xaf6000`. The SDK build name is `1.5.20-win32`.

## Lua initialization

The previous executable kept the game Lua state in EDI at its hook. The new
initialization keeps it in ESI. It also removes `dofile`, `loadfile`, `load`
and `loadstring` from the game's Lua globals. Integration runs afterwards at
`0x446633`, replaying the complete twelve bytes
`6a20c78564ffffff00000000`. No part of the following instruction is overwritten.
The bridge does not restore the removed globals.

## Native profile

Lifecycle vtable: `0xd34114`, entries `0x7733a0`, `0x77f330`, `0x77f3c0`.
Carrier vtable: `0xd589d0`. Stock UI insertion: `0x5b56bb`.
The incoming waypoint hook is `0x7785b7`; its vehicle remains in EAX, server
in ESI, sender at `[EBP-0x36c]`, and peer roster at server `+0xc48+0x20`.
The signed sender is checked against that live roster before attribution.

Native instruction guards and the existing static verifiers were ported
together, including calls, vtable references, stack cleanup and HIGHLOW
relocation operands. Addresses were compared against the retained old
executable and checked in the new executable; no uniform address shift is
assumed. Unknown executable hashes still leave the SDK inactive.

## Validation scope

Read-only executable checks and native/Python fixtures do not certify gameplay.
Consult the release's `RELEASE.json` and exact binary acceptance report for the
results of isolated game loading and any ordinary-host checks. Remote
multiplayer, dedicated servers, macOS native support and complete gameplay
acceptance remain unverified for this port unless explicitly recorded there.

This release targets the updated game. Older SDK releases remain downloadable
for older builds. ModLoader must independently recognize the new game before
it can use the signed update; do not bypass its game or runtime checks.
