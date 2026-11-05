# Carrier docking and deck recovery (0.9.0 development)

Optional capabilities `carrier-dock-v1` and `carrier-recovery-v1`. They are not
present in published 0.8.1. The SDK adds no automatic recall or braking policy.
The implementation adapts the checked engine mechanisms researched by
Yolo / totolerigolo118 in Brake v15 at upstream commit
`a40d1572eabd6d9710e6d64aae431824ffc98790` (GPL-3.0-or-later).

## Lua

```lua
local cc2 = require('cc2')
if cc2.carrier.dock_available() then
    local ok, reason, request = cc2.carrier.dock_request(carrier_id, aircraft_id)
    -- Later update: acceptance above only queues SDK work.
    local result = ok and cc2.carrier.dock_result(request)
end
if cc2.carrier.recovery_available() then
    -- Arm after the game reports dock_state=5 for this carrier, while parked.
    local ok, reason = cc2.carrier.recovery_arm(carrier_id, aircraft_id, {
        window_ticks = 300, grants = 3, touch_jumps = 30,
    })
    local status = cc2.carrier.recovery_status(aircraft_id)
    cc2.carrier.recovery_disarm(aircraft_id)
end
```

These are independent-module functions; game-owned screens still communicate
through the existing module request/state mailbox. Early rule VMs receive no
carrier mutation API. Old runtimes return unavailable from the optional facade.

`dock_result` returns `request_id, generation, carrier_id, vehicle_id, code,
status`. Status is `pending`, `queued`, `already_queued`, `invalid_vehicle`,
`profile_refused`, `engine_refused` or `engine_fault`. A queued game order is
not proof of landing, take-over or stowage. Read world docking state separately.
The engine is called at most once per request. An identical pending vehicle
request is refused across owners. SDK limits: 16 pending, 4 per owner, 64 retained
results; older completed records may be evicted. Results never survive a session.

`recovery_status` returns `vehicle_id, carrier_id, generation, armed, grants,
touch_jumps, remaining_ticks, reason`. Reason constants are in
`include/cc2x_carrier.h`. Unknown/foreign records return nil and an error.
Only one aircraft can be armed at once. Repeated arm calls do not renew a window;
even disarm, timeout, Lua error or module reload cannot reopen it while the game
continues that same queued/docking recall. The game must leave that recall first.

## Engine contract and guards

Supported profiles: original Windows x86 1.5.18 and Steam r2. Exact core profile,
PE timestamp/machine/image size, carrier vtable/entry and instruction signatures
are checked. Runtime discovery reads the sites; it never patches them. First arm
requests all four hooks in one transaction, refused if another plugin owns a site.
An installed legacy `cc2x_frein.dll` therefore keeps its sites. No hook is activated
by installing the SDK or discovering a capability.

The carrier's actual dock method runs on a later server tick, resolving IDs and
rechecking the session, carrier class, aircraft class, same team, current docking
state, vtable and exact entry. An uncertain native exception latches the backend
unavailable for the rest of the process. This is local server work, not a network
RPC or a multiplayer permission grant. Retain IDs, never engine pointers.

Recovery additionally requires no pilot in control mode 1, no attachment, all
wheels touching (>=0.01), finite body data, relative speed <=1 m/s, within 300 m
and 50 m vertically of the selected carrier, and a free class-1 hangar bay.
These are conservative backend limits; a mod may apply tighter deck criteria.
They are rechecked inside hooks. The stock engine still performs its own touch,
bay, joint and push-back operations. The SDK writes no position, engine power,
brake, landing queue element or stock runway ownership directly.

Maximum window: 600 simulation ticks for planes, 300 for helicopters; default
300 for either. Maximum three plane runway grants, 30 plane touch-test jumps,
one helicopter runway grant. Counters decrement inside hooks. H1 uses a private
queue-element copy; H2 requires H1's manager in that iteration; requeue state 3
after a grant stops the window. H5 checks runway state 3 and the armed owner,
plus <=2 m/s from the game's frame. H6 retains the stock first-helicopter grant
block. All XMM registers are preserved. Session end clears requests/windows;
module removal and failed Lua callbacks cancel that owner's pending work/window.

## C and compatibility

Include `cc2x_carrier.h`, resolve `cc2x_carrier_from_core(api)` and check for null.
The named export is `cc2x_query_carrier_v1`. Native owner names start with
`native.`; supply the current framework session generation. Lua ownership and
generation are implicit. Both languages use the same backend. All calls that
touch world state require the current server thread; discovery is read-only.
Stable `cc2x_api` (296 bytes on x86), `cc2x_framework_api` and the earlier named
session/order interfaces keep their layouts. Independent Lua remains API 2.

## Acceptance

Native synthetic fixtures and paired real Lua validate API behavior, not flight.
`tools/verify_carrier_profile.py EXE` performs a read-only static r2 check and
records executable/signature evidence without running the game. Actual game
acceptance is still required: Albatross/Manta and Razorbill/Petrel, map recall
while held, auto-recall after leaving the pilot seat, occupied bay, pilot return,
moving carrier, cancelled recall and x1 simulation. Remote host/client and
dedicated-server behavior remain unverified. The older native mod's reported
single-player results do not certify this new runtime or a Lua policy port.
