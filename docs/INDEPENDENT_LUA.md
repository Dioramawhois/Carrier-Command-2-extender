# Independent Lua mods in the shared SDK

The development CC2X runtime embeds Lua 5.3.6 and gives each activated module its
own VM, `cc2` facade and relative `require` cache. The original game-owned Lua
states still use `cc2x`. Stock `update_*` closures are not imported into an
independent VM. The two interfaces share verified native operations; neither
creates multiplayer RPC or arbitrary access to game addresses.

## Package and lifecycle

Use the existing CC2 package identity with `mod.xml`, and preferably:

```text
content/bridge.json
content/bridge/main.lua
content/bridge/helper.lua
```

```json
{"id":"example.module","scripts":["bridge/main.lua"]}
```

```lua
local cc2 = require('cc2')
local helper = require('bridge.helper')
return {
    on_load = function() print('loaded') end,
    on_update = function(dt) helper.update(dt, cc2) end,
    on_unload = function() print('unloaded') end,
}
```

The current reader prefers `content/bridge.json`, then the older
`content/native_bridge/bridge.json`, then package-root `bridge.json`. The latter
two still support schema 1 with `id`, `name`, `version`, `runtime: "bridge"`,
relative `entry`, and `hooks`. A malformed preferred manifest fails that package;
it does not fall back to another manifest. Paths stay inside the selected code
root. Game-owned `content/scripts` entries, traversal, reparse points, duplicate
keys and ambiguous owner IDs are rejected. Independent code is not an ordinary
resource overlay to merge with another module's same-named entry file.

Only packages in the validated startup activation snapshot are discovered;
subscription/download alone never enables code. The same snapshot is prepared
before native plugin loading. See [module bundles](MODULE_BUNDLES.md) to retain
several modules inside one active merged game package. Explicit indexes preserve
the selected order; standalone legacy discovery otherwise sorts owner IDs.

## Facade

| Operation | Result and boundary |
|---|---|
| `cc2.carrier.throttle(step)` | -1, 0, +1; queues a relative power-lever increment, returns success/status. Acceptance is not execution. |
| `cc2.animation.routes(n)`, `.runway(n)`, `.lift(n)`, `.crane(n)` | Positive integer factors; x1 restores stock arithmetic. Factors are process-global; there is no ownership/arbitration promise. |
| `cc2.carrier.hangar_slots(carrier_id, chassis_type)` | Copied carrier-specific zero-based slots, compatibility and occupancy; available in an active server callback. |
| `cc2.carrier.spawn_in_hangar(carrier_id, slot, chassis_type)` | Queued verified carrier operation; returns request ID. Revalidates type, slot and occupancy on execution; does not debit workshop inventory. |
| `cc2.carrier.hangar_spawn_result(request_id)` | Completion record or pending/expired reason. Results from a previous world are discarded. |
| `cc2.input.key_down(vk)` | Foreground-only Win32 key sample, not an event queue. Hotkeys belong to the mod. |
| `cc2.bridge.reload_mods()` | Deferred VM reload using the same startup activation snapshot. |
| `cc2.bridge.is_ready()` | Current host execution context, not a guarantee that every native feature is available. |
| `cc2.world` | Copied world-read facade over the checked shared API; no raw engine pointers. See sdk/cc2.lua and WORLD_API.md. |

Native operations require their independently verified game profile. Unsupported
operations return unavailable/refusal; a live VM does not imply all hooks exist.
Session transitions/reload clear pending host commands. Dedicated/client network
delivery and permissions need separate implementation and live acceptance.

## Generic vehicle and UI declarations

Before game definitions load, schema-2 scripts are evaluated in a restricted,
disposable declaration VM. Only declarative registration is available there:

```lua
local cc2 = require('cc2')
local identity = assert(cc2.vehicles.register({
    id = 'aircraft', name = 'Example aircraft', base = 'wing_light',
    definition = 'game_objects/example_aircraft.xml',
    ui = 'aircraft', -- optional key in manifest ui.vehicles
}))
```

The XML must exist safely inside this module's content root. Supported native
bases are the verified `wing_light` and `wing_heavy` families. Identity is
`owner.local_id`; module ownership comes from its manifest. Optional `order`
0..65535 ranks declarations before normalized namespaced identities. This is not
an immutable numeric ID pin: changing the active catalog can invalidate saves.
At runtime registration resolves the existing declaration and cannot mutate it.

`cc2.vehicles.lookup(identity)`, `.list()` and `.is_ready(identity)` report the
catalog. `.spawn_in_hangar({type=identity, carrier=id, slot=index})` queues the
same checked carrier operation using stable identity. It is not an arbitrary
world factory. Private Lua/closures from the declaration VM are discarded.

Manifest `ui.vehicles` maps local keys to safe PNG `small`, `large`, `loadout`
paths. `ui.weapons` can map a symbolic existing stock attachment `type` to icons;
it does not create new weapon classes. Runtime registries allocate namespaced
atlas regions without embedded aircraft-specific branches. Game-owned Lua sees
ready definitions through `cc2x.vehicles.definitions()` and weapon mappings via
`cc2x.ui.weapon_icons()`. No old experimental prototype endpoint is required.

Fresh-checkout SDK tests use trusted facade code and synthetic fake-game mods.
Fresh gameplay, saved-catalog migration and host/client/dedicated acceptance of
the integrated runtime remain open; historical team fixtures do not close them.
