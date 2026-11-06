# C, C++ and Lua capability parity

The unpublished 0.10 branch adds [motion API 1](CARRIER_MOTION.md): named
`cc2x_motion_api.available/read/set/contacts/drive` and independent Lua
`cc2.carrier.motion_available/motion_state/set_motion/contacts/drive_motion`. Both use the
same guarded Motion backend. Native fixtures cover the profile, seven-word ABI,
ownership, power cap, engine-gated forward command, replicated helm factors,
guidance, manual takeover, expiry and visibility admission. The addon
also exercises its Lua adapter with synthetic telemetry; this is not a full
live-combat or remote-client acceptance claim.

The 0.9.0 development candidate adds the separately named
[`cc2x_carrier_api`](CARRIER_RECOVERY.md) and
[`cc2x_settings_api`](MODULE_SETTINGS.md), with matching `cc2.carrier.*` and
`cc2.settings.*` functions over shared backends. They do not change the existing
C prefix/framework layouts or independent Lua API 2. Discover each optional
extension; the published 0.8.1 binary does not provide them.

CC2X 0.7.0-client-beta.1 provides stable C prefix API 8, named framework C API 1
and independent Lua API 2. These additions are **not** in the older
0.6.0-client-beta.1 runtime. See the release assets for the exact source,
binary hashes and validation scope.

The SDK supports native C/C++ plugins and Lua modules. Both interfaces use the
same implementation for supported engine operations. Gameplay rules, button
names, timers, key assignments and user interfaces belong to optional mods.
The framework does not select or activate a gameplay feature by itself.

The published 0.8/0.8.1 runtime additionally provides [waypoint origin](ORDER_ORIGIN.md):
the separately named `cc2x_orders_api.available/current` and `cc2.orders.*`
read one checked backend. This optional extension is not in the published 0.7
binary and cannot be inferred from C API 8 or Lua API 2 alone.

## Capability map

| Capability | C / C++ | Independent Lua | Shared implementation / regression |
| --- | --- | --- | --- |
| Live vehicles, islands, airfields, waypoints, fields and ID queues | `cc2x_api.find/count/ref/geti/getf/seti/setf/list_*` | `cc2.world.find/count`, selector `geti/getf/ref/count/seti/setf/list_*` | Core world API; `runtime_integration_test`, `framework_parity_test` |
| Physics, raycasts, brakes, gear, island landing and waypoint replication | `body_get/body_set/raycast/vehicle_brakes/vehicle_gear/request_landing/waypoints_sync/airfield_*` | Selector `body/body_set/brakes/gear/request_landing/waypoints_sync/bay/to_world`, `world.raycast` | Core operations; `runtime_integration_test` |
| Synchronous world and construction decisions | Existing `on_*` callbacks or framework `subscribe` | `cc2.events.on/off/available`; early decisions in a separate `rules` entry | Host event dispatcher; runtime and paired button fixtures |
| Copied named controls and staged normal control messages | Framework `button_count/button_get/buttons_set` | `cc2.world.buttons/set_buttons` | One checked inventory and transaction; `framework_buttons_parity_test` |
| Immediate alternative button read | Return a button index from `vehicle_control_query` | Return an index from the same event | One checked read alias; paired button fixture |
| Wing throttle output | Framework `throttle_get/throttle_set` | Vehicle selector `throttle()` / `throttle(value)` | Same guarded profile/type/range implementation; paired framework and control fixtures |
| Foreground key state | Framework `key_down` | `cc2.input.key_down` | Host input sampler; paired framework fixture |
| Module requests and published numeric states | Framework mailbox and message functions | `cc2.messages.poll/send/read/publish/forget` | One bounded mailbox; bidirectional paired tests, session/removal/overflow fixture |
| Next-tick work, cancellation and stale-session rejection | Framework `session_generation/session_tick/post/cancel`; legacy `cc2x_session.h` remains supported | `cc2.session.generation/tick/post/cancel` | Shared Host task queue; paired fixture checks nesting, cancellation, quota and reused server addresses |
| Metadata-only user logic types | Framework `entity_register/remove/clear/count/get` | `cc2.entities.register/remove/clear/count/types` | Same owner-scoped registry; paired framework fixture |
| Custom vehicle definitions and UI images | Framework `declare_vehicle/declare_ui` during `plugin_load` | Early `cc2.vehicles.register` plus manifest `ui_images` | Same field, ownership, path and collision validators and immutable catalog; declaration and paired framework fixtures |
| Typed custom-vehicle spawning and catalog lookup | Framework `vehicle_spawn/vehicle_definition_count/vehicle_definition_get` | `cc2.vehicles.spawn_in_hangar/lookup/list/is_ready` | Same definition registry and queued hangar operation |
| Hangar availability and spawn completion | Framework `hangar_slots/hangar_result`, existing `carrier_hangar_spawn/on_hangar_spawn_result` | `cc2.carrier.hangar_slots/spawn_in_hangar/hangar_spawn_result` | Same Host slot descriptions, request queue and structured results; runtime fixture |
| Carrier power lever and animation multipliers | Existing `carrier_throttle/animation_speed` | `cc2.carrier.throttle`, `cc2.animation.*` | Same callbacks; runtime fixture |
| Native library functions callable by independent modules | Framework `vm_bind/vm_unbind` with its own stack API | `cc2.native[owner][function]` | Same VM binding registry, argument/return checks and revocation; paired fixture |

The rows describe available interfaces. A row is not a claim that every real
game behavior has been accepted. Paired tests execute real Lua against synthetic
game objects and native calls. They do not replace game, remote multiplayer,
dedicated-server or clean-machine tests.

## C and C++ consumers

Include `cc2x.h` for the stable prefix and `cc2x_framework.h` for new operations.
Inside `cc2x_plugin_load`, use:

```c
const cc2x_framework_api *framework = cc2x_framework_from_core(api);
if (!framework) return 0; /* An older or different runtime: fail safely. */
```

This resolves the named extension from the already loaded module that supplied
`api->server_post`. It does not guess a DLL filename, load another library or
reinterpret a longer fork-specific API struct. Version, size and interface ID
must match. The original API 8 prefix remains 296 bytes on Windows x86.

Check the relevant capability bit, and still check every operation's result.
For example, button capability discovery is read-only. The actual subscription
revalidates bytes and may refuse if another native plugin has already taken
one of those hook sites. Discovery must not occupy a legacy plugin's sites.

Use `examples/framework_c_plugin/framework.c` or the C++ equivalent in
`examples/framework_cpp_plugin`. Both register a small optional Lua function
without changing gameplay. C++ exports must have `extern "C"` linkage.
The `build.py` `plugin(source, output)` helper supports C11 and C++17 sources.

Native plugins remain separate DLL packages under `content/cc2x`; no mod DLL
is merged into the SDK. ModLoader's native profile/hash review still applies.
Packages using the new named C extension declare `framework_api="1"` alongside
`sdk="cc2x" api="8"` in their `cc2mce` metadata, or `"framework_api": 1` in the
packager metadata. The matching ModLoader requires both reviewed
`runtime_framework_api: 1` and `framework-c-api-v1`; the older API 8 runtime
cannot silently satisfy this requirement. A mixed C/Lua package declares both
`framework_api` and `lua_api`. Neither declaration approves an unknown DLL.
New native build reports record all public header hashes, including the named
framework extension, rather than recording only the stable prefix header.

## Lua consumers and native libraries

An independent module starts with `local cc2 = require('cc2')` and checks
`cc2.lua_api_version >= 2`. It declares its Lua API requirement in package
metadata. A matching reviewed runtime is required; a shared C API number alone
does not prove that a Lua feature is available.

For the C example above:

```lua
local cc2 = require('cc2')
return {on_load = function()
    assert(cc2.native.example_c.sum(2, 3) == 5)
end}
```

Bindings are registered before independent VMs load. They receive an opaque
`cc2x_vm` and its own stack API. It is not a game-owned `cc2x_lua`: never cast
one to the other. A retained VM handle is rejected outside its current native
callback. Revoked bindings return an unavailable result.

Game-owned screen Lua uses the ordinary `cc2x` bridge. It exchanges requests
and published states through `cc2x.modules`; it does not share a VM or engine
pointer with the independent module. The gameplay module validates requests.

## Thread, session and ownership rules

World operations require an active local server and its game thread. Native
authors retain IDs between ticks, resolve objects again and use a session-safe
queue from other threads. Lua selectors perform those lifetime checks for the
caller and refuse stale or removed objects. No local queue is a network RPC.

The shared task queue accepts at most 256 tasks, with at most 64 per owner.
Native `post(owner, generation, callback, user)` and `cancel(owner, task)` use
an explicit `native.` owner namespace. Lua ownership is the calling module;
one C plugin does not consume every other plugin's individual quota.
Tasks posted during a tick run on a later tick; nested posts cannot execute
recursively in the same tick. Cancellation is owner-scoped. A session change
invalidates pending work even if the server reuses its old address. Lua registry
references are released on the next serialized runtime tick or module teardown;
no Lua callback executes from the session-end notification thread.

Mailbox queues and states clear on session changes. Lua metadata belongs to
the loading module and is removed on teardown or failed initialization. Native
metadata owners use `native.` names and can remove their own records; Host
shutdown removes remaining native records. Metadata does not instantiate a
new engine object type.

Early vehicle/UI catalogs are process-lifetime data. C declarations are staged
around the native loader boundary: a failed `plugin_load` discards declarations
and shared registrations. Both languages reject late declarations, collisions,
unsafe resource paths and unsupported bases. Catalog changes require a restart.

## Boundary and remaining work

Equal access to supported engine operations does not mean identical language
privileges. Native code can access memory or perform arbitrary OS operations;
those powers are not exposed to Lua. Independent Lua has no `io`, `os`, native
library loader or arbitrary memory-patching API. New engine research should
become a checked shared primitive with public C and Lua access and paired tests.

The 0.9.0 candidate adapts Brake v15's private carrier engine mechanisms into
the optional shared carrier interface; its gameplay policy remains a separate
Lua-mod migration. Paired synthetic tests do not establish that the native
mod's historical game outcomes carry over to this new runtime. Full migration
and fresh gameplay acceptance remain gates for the gameplay-mod releases.

Do not reclassify a C gameplay implementation as a framework capability merely
by giving it a Lua enable switch. Shared engine mechanisms belong here; each
mod's policy remains optional and separately distributable.
