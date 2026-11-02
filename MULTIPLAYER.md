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
