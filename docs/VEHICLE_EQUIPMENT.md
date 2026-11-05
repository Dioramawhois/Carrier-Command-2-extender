# Optional parked-vehicle equipment API

Unreleased 0.9.0 development interface; stable API 8, framework 1 and Lua API 2
retain their layouts. This is an engine primitive; mods own team permissions,
airfield membership and their gameplay economy.

Independent Lua:

```lua
if cc2.equipment.available() then
    local ok, reason = cc2.equipment.set_attachment(vehicle_id, slot, definition, true)
end
```

Native plugins resolve `cc2x_query_equipment_v1` using
`include/cc2x_equipment.h`. Call `available()` and `set_attachment()` on the
server thread, inside the current session; use the established server task
interface when arriving from another thread. Calls are synchronous. Lua defaults
`fill` to true. Slots are zero based; `definition=-1` clears a hardpoint.
Only the supported game's stock attachment catalog is accepted.

The operation requires a docked, unattached vehicle with no waypoints, bounded
hardpoint arrays and a stationary body (each velocity component <=0.5 m/s).
It validates the hardpoint type against the native catalog before replacement.
Creating an attachment is free: it neither debits nor refunds any inventory.
With `fill=true`, the new attachment starts full; other vehicle stores are
unchanged. Inventory accounting belongs to a later, separate mod feature.

The reviewed Windows Steam r2 adapter authenticates the exact executable SHA-256
and relocation-aware code signatures. It calls the stock attachment constructor
and replacement path, then the stock state serializer and attachment event for
connected peers. No new network packet or screen impersonation is introduced.
Unsupported builds return unavailable. A native fault latches refusal until the
next session; no automatic retry or speculative rollback follows a partial call.

Offline evidence covers admission rules, C query contracts, Lua argument checks,
unavailable-profile refusal and the existing runtime regressions. These tests do
not execute the setter in the game. Fresh replacement/clear/ammunition/save-load
and remote replication acceptance is still required before public release.
