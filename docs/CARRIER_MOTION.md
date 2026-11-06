# Optional carrier motion API 1

CC2X 0.10 development interface, staged for release, for the reviewed Windows
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
| drive(owner, generation, id, power_cap, forward) | cc2.carrier.drive_motion(id, power_cap, forward) | Renew native steering with an explicit normalized forward command, bounded by the selected cap |
| contacts(carrier, out, capacity, count) | cc2.carrier.contacts(carrier) | Copied currently detected contacts for the owning local team |

State speed is m/s, horizontal coordinates are x/z. Lua flags are active, manual,
engine_on and can_control. The cap is finite 0..1 normalized native forward jet
output. A cap of 1 permits full native output; this interface has no knot maximum
or hull-specific speed table. Drive lets a consumer avoid native turn-in-place
idle while retaining native steering. Actual output is forward multiplied by
normal engine output, with native fuel (greater than 0.05), engine and disabled
gates. No transform, engine output/RPM or body velocity is written.

Read additionally reports signed heading_error (radians to the current native
path target), path_distance (metres), yaw_rate and steering. guidance_ready admits
valid mode-0 native guidance; consumers must not drive ahead from an unavailable
target. Speed/turn/approach policy belongs to the consumer, not this API.

The backend publishes live steering and commanded forward power through the
native replicated control_factors fields. Stock/Revolution helm-wheel and
throttle-lever animation and instruments consume the normal data. This does not
replace carrier XML or install a client mesh hook. A manual takeover stops these
automatic writes; explicit release neutralizes the factors normally.

Motion API 1 is still unpublished: alpha.3 finalizes its initial state/table
layout with drive/guidance. An older private alpha.2 DLL is refused by current
size/identity discovery and cannot supply drive_motion. Existing released core,
framework and Lua API layouts are unchanged.

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
signed output caps, manual input, expiry and visible-only contacts. The owner
accepted Carrier Navigator alpha.2 in a private legitimate game copy with
Revolution. That historical confirmation does not cover the later alpha.3
helm/continuous-power revision; it requires the owner's new in-game check.
Release tooling preserves the accepted build record and verifies every compiled
input against the source archive.
This is not certification of live combat, countermeasure expenditure, a remote
client, dedicated servers, macOS runtime or ordinary-install activation.
