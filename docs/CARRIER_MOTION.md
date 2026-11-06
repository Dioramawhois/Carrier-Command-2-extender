# Optional carrier motion API 1

Unpublished CC2X 0.10 development interface for the reviewed Windows
1.5.18-win32-r2 executable. Stable C API 8, framework API 1 and Lua API 2 layouts
remain unchanged. The published 0.9 runtime does not provide this interface.

Include `cc2x_motion.h` and resolve `cc2x_motion_from_core(core)` from the already
loaded core. Validate version, size and interface ID. This uses the separately
named `cc2x_query_motion_v1` export; never reinterpret a longer core structure.

| C function | Independent Lua | Result |
| --- | --- | --- |
| available() | cc2.carrier.motion_available() | Checked profile availability; no hook installation |
| read(id, state) | cc2.carrier.motion_state(id) | Copied generation, carrier/team, mode, waypoint count, state flags, speed, lever, forward power, native target |
| set(owner, generation, id, active, power) | cc2.carrier.set_motion(id, active, power) | Own or renew a local navigation lease; Lua owner/session are supplied by Host |
| contacts(carrier, out, capacity, count) | cc2.carrier.contacts(carrier) | Copied currently detected contacts for the owning local team |

State speed is m/s, horizontal coordinates are x/z. Lua flags are active, manual,
engine_on and can_control. The cap is finite 0..1 normalized native forward jet
output. A cap of 1 permits full native output; this interface has no knot maximum
or hull-specific speed table. Normal native pathfinding, turns, engine breakers
and hull physics remain active. No transform or body velocity is written.

Set requires a live own-team carrier, matching session, the server thread and
exactly one peer. Other-team/AI/dead carriers, remote sessions, absent routes,
nonfinite/out-of-range power and competing owners are refused. The first active
request transactionally wraps native carrier motion slot 57. Its native return
pops seven stack words; both entry/branch and return guards are checked.

Renew every server tick. More than three missed ticks, empty routes, authority
loss, unload/error or explicit release stop the lease and neutralize propulsion.
Physical helm/throttle input takes priority and restores manual mode without
zeroing the player's input. The consumer must require an explicit resume after
manual takeover or lost ownership. A session change drops old IDs and leases.
The backend does not supply route editing UI or save a separate route file.

Contacts are admitted through the team's native current-visible observation
record, not persistent discovery. The native detection flag covers radar/visual
observations; no separate provenance is invented. Vehicle/projectile metadata is
read only after visibility admission. C contacts contain id, kind, definition,
team and xyz; Lua adds detected=true. Failure returns no valid sensor snapshot.
Target selection, threat ranges and weapon controls belong to mods.

Native fixtures check the ABI, profile refusal, team/owner/session restrictions,
signed output caps, manual input, expiry and visible-only contacts. Initial
native route motion ran in a private legitimate game copy with Revolution.
This is not certification of live combat, countermeasure expenditure, a remote
client, dedicated servers, macOS runtime or ordinary-install activation.
