# Incoming waypoint origin (0.8 development)

This read-only shared C/Lua primitive identifies the current engine waypoint
receiver. It is not in SDK 0.7, not a general network transport, and not a grant
of team/admin permissions. It adds no gameplay policy. The stable C prefix,
framework API 1 and Lua API 2 retain their layouts; the optional named extension
and `waypoint-origin-v1` capability identify this addition separately.

## Public interfaces

Native consumers include `cc2x_orders.h` and resolve the immutable named table
from the module that supplied their core API:

```c
const cc2x_orders_api *orders = cc2x_orders_from_core(api);
/* Inside the synchronous incoming waypoint callback: */
cc2x_waypoint_order order = {0};
order.size = sizeof(order);
if (!orders || !orders->available() || !orders->current(&order)) return;
/* order.vehicle_id, order.peer_id, order.generation, order.flags are copies. */
```

Independent Lua uses the same backend and the existing `waypoints` event:

```lua
local cc2 = require('cc2')
assert(cc2.orders and cc2.orders.available())
assert(cc2.events.on('waypoints', function(vehicle_id)
    local order = cc2.orders.current()
    if not order or order.vehicle_id ~= vehicle_id then return end
    -- Validate the mod's own policy before changing the resolved vehicle.
end))
```

`available()` reports a supported native receive layout with passing byte guards.
The read-only availability check is also permitted in the early rules VM.
The rules VM has no current-order/world access. Outside an incoming callback,
Lua `current()` returns `nil, "no_verified_incoming_order"`; C returns 0 without
changing the output. Unknown older runtimes return unavailable or no extension.

## Snapshot contract

- `vehicle_id`: the actual engine vehicle at the audited callback site.
- `peer_id`: an engine peer ID found exactly once in the receiver's peer roster.
  Zero is a valid ID. This is not a Steam ID or an authenticated team/admin flag.
- `generation`: the local **core** epoch that accepted this incoming order. It
  is distinct from the framework Host's `cc2.session.generation()` and is never
  a shared network session token. Each domain performs its own lifetime checks.
- `broadcast_pending` (Lua), `CC2X_ORDER_BROADCAST_PENDING` (C): the engine will
  broadcast the final waypoint queue after this callback returns. Do not publish
  an additional copy of the same synchronous change. Deferred/direct operations
  outside that receive handler still need their own explicit publication.

The snapshot exists only on the callback's thread, with its original live core
epoch. Nested unavailable contexts hide an outer context; returning restores it.
A changed or absent session invalidates even a retained same-address snapshot.
No engine pointer, VM handle or queue storage escapes through this interface.

## Native evidence and profile boundary

The first supported receive profile is Windows x86 1.5.18-r2, EXE SHA-256
`461e12623bacb7de29ca27b4614425c87e8df2c8327a5e958a957c850effe42a`.
`integrations/waypoint-origin-profile.json` pins five instruction windows:
the peer-manager selection, incoming source output, waypoint-case gate, peer
lookup and final broadcast. The build generates the runtime guard array from
that contract. No client offsets are applied to dedicated_server.exe or to the
older client profile. An unavailable optional context leaves the legacy event
ABI unchanged; a consumer requiring the context must refuse that action.

The current receiver uses `ESI = server`, `EBP-0x36c = source engine peer ID` and
`EAX = vehicle` at the waypoint hook. The peer manager is `server+0xc48`, while
the lookup's actual peer deque is at **manager+0x20**. Its initial deque can be
empty in a local session. A fresh private-game memory inspection and current
lookup instructions exposed and corrected the earlier manager/deque confusion.
Decoder regression fixtures leave the manager's first deque empty deliberately.

The adapter copies checked readable memory, caps roster traversal, rejects
malformed buffers, duplicate/missing peers and mismatched hook/server identity.
The native fixture tests that decoder and the public table; paired framework
fixtures execute real Lua and C reads of the same scoped snapshot. These are
offline checks. Live host command receipt and independent remote-peer delivery
require separate logs from the exact runtime being tested. Team/admin authority,
host capability negotiation and arbitrary replicated module state remain open.
