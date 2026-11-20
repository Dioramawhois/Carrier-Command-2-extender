# Optional surface navigation API 1

CC2X SDK 0.11.0-dev source candidate, Windows 1.5.18-win32-r2, local single-peer
sessions. This optional interface changes only explicitly configured carriers.
Stable C API 8, framework API 1, Lua API 2 and motion API 1 remain unchanged.
The published 0.10 runtime and other vessels retain their existing routing.

## Why this is separate from motion

The reviewed game's carrier and barge branches use the same deep-water mesh
classification. Its native class flag admits a triangle only when every vertex
is below the fixed -100 mesh-elevation threshold. The carrier also uses a 150 m
node/arrival proximity. Near a coast, the engine can project a waypoint onto its
mesh or finish an unsuccessful route at the nearest visited triangle and then
consume the waypoint. These mechanisms can change the apparent destination.
They are static findings for the exact executable, not a fresh gameplay result.

This interface retains native path search, funnel and steering while selecting
water-triangle eligibility and arrival radius within one owned request. It does
not alter shared mesh flags, global constants, physical hull properties, body
position/velocity or the routes of unconfigured carriers, barges or AI ships.

## C and Lua contract

Include `cc2x_navigation.h` and use `cc2x_navigation_from_core(core)`. Resolve the
separate `cc2x_query_navigation_v1` export and validate version, size and ID.
The Windows x86 table is naturally aligned to 32 bytes; options are 24 bytes
and copied state is 56 bytes. Do not reinterpret the longer table as motion API 1.

| C | Independent Lua | Operation |
| --- | --- | --- |
| `available()` | `cc2.carrier.navigation_available()` | Checked installed profile/seams; no movement |
| `configure(owner,generation,id,options)` | `cc2.carrier.set_navigation(id,options)` | Configure before motion lease/drive; no movement |
| `read(id,state)` | `cc2.carrier.navigation_state(id)` | Copied exact-goal state, not the intermediate native path target |

Lua requires an explicit plain options table. `{}` enables surface policy with
`minimum_depth=0`, `arrival_radius=25`; `enabled=false` releases the policy.
Only these three string keys are accepted. Unknown keys, metatables, coercible
strings, nonfinite numbers and extra arguments are refused.

C options require exact struct size. `flags=CC2X_NAVIGATION_SURFACE` enables;
`flags=0` disables. Unknown flags are refused. Minimum depth is finite 0..1000 m;
arrival radius is finite 1..150 m. Bounds apply even to disabled options. Lua
supplies its module owner and current generation; C callers use a valid native
owner. Another owner cannot replace a live policy or motion lease.

Minimum depth classifies mesh vertices relative to the island elevation. A zero
value permits strictly wet triangles rather than the native deep-water subset.
It does not establish under-keel clearance, hull-side clearance or a depth chart.
Mixed land/water triangles remain excluded. A raw goal must lie in admitted
geometry; it is not silently snapped to another location. An unavailable or
failed path is blocked and retained instead of being marked reached.

Copied state has `id`, `waypoint_id`, `target_x`, `target_z`, `distance`,
`minimum_depth`, `arrival_radius`. The target is the requested current/last
waypoint and distance is its direct horizontal range. Flags are `configured`,
`active`, `ready`, `blocked`, `arrived`, `faulted`. Arrival applies to the terminal
empty route; intermediate/repeat routes continue to their next native point.

Configure idempotently, admit using a zero-power motion lease, then drive only
after `ready`. Motion release, manual takeover, expiry, owner unload and session
change clear the policy. Errors never enable the old deep-water fallback for an
opted-in request. Engine/fuel/disabled-state and single-peer authority checks
continue through the existing motion backend.

## Native boundary and verification

Checked seams are installed together before a session runs path queries. Scoped
request context is confined to the opted-in carrier's follower call and restored
through nesting, cancellation and guarded native faults. Outside that context,
the displaced native instructions and original calls retain their behavior.
Floating state, register effects, instruction boundaries, return cleanup and
overlap/commit failures need separate native fixtures and exact-EXE guards.

Tests use owned synthetic geometry and native fixtures, real Lua contract checks
and read-only executable inspection. A candidate build does not establish coastal
navigation in the live game. Close approach, stopping under Revolution physics,
route save/load, host/client and collision clearance require separate acceptance.

In particular, the original engine tick precedes module updates. On a newly
loaded session a saved auto-mode carrier might encounter stock projection before
its module configures this optional policy. First-frame vehicle scheduling has
not been established, so exact coastal-point retention on reload is unverified.
This interface does not change global tick order or suppress unowned projection.
