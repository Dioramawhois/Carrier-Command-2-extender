# Framework boundary and Lua gameplay mods

The SDK is an engine framework. With no selected gameplay modules it must not
choose flight modes, apply brakes, recall aircraft, synchronize named copies of
buttons, add aircraft or enable human-island rules. Each such decision belongs
to a separate optional mod. Native layout checks and dormant hooks implement
engine capabilities; they do not enable a feature by themselves.

C/C++ plugins remain a supported development route. Shared engine operations
must be accessible from public C and Lua interfaces to the same implementation.
Our supplied Lua/content packages require no private DLL; authors choosing C
continue to package their plugins separately. The new named C interface in
`include/cc2x_framework.h` is exported by the 0.7.0 client beta; it is absent
from 0.6.0 and older runtimes. See [the C/Lua capability audit](C_LUA_PARITY.md) for
the operation map, paired tests, lifecycle rules and remaining migration gaps.

## API additions in 0.7.0-client-beta.1

The older `0.6.0-client-beta.1` binary does **not** implement these APIs. Do not publish a mod
requiring these calls as compatible with that older binary.

* `cc2.events.available(name)` and `cc2.events.on(name, function)` expose checked
  synchronous engine callbacks to each independent module. `off(name)` removes
  that module's handler. Events are `waypoints(vehicle_id)`,
  `airfield_tick(tile_id, zero_based_index)`,
  `hangar_choice(vehicle_id, tile_id, index)`, `vehicle_controls(vehicle_id)` and
  `vehicle_control_query(vehicle_id, source_button_index)`. Construction-time
  `island_controller(team)` / `island_spawn(tile_id, ai_team)` use the separate
  rules entry described below.
* Decision events return a hangar slot, `-1`/nil for the engine's choice, or a
  boolean/0/1 for controller/spawn decisions. Unsupported callbacks, absent
  modules and failed callbacks retain the engine's neutral decision. Conflicting
  non-neutral hangar choices are refused rather than silently prioritised.
* Vehicle selectors expose `vehicle:throttle()` and `vehicle:throttle(value)`.
  This is the checked wing engine output, not the general control-power field.
  Values must be finite and between 0 and 1. Every call checks its instruction
  profile and live wing-vehicle type. No ECO/FLANK constants or aircraft lists
  are part of the SDK.
* During `vehicle_controls` only, `cc2.world.buttons(vehicle_id)` returns copied
  `{name, index, on, enabled}` entries. `cc2.world.set_buttons(vehicle_id, values)`
  stages up to 64 named boolean states. A successful return means **staged**.
  The engine boundary validates every write and peer queue, then broadcasts the
  corresponding normal control-state messages. Disabled controls, ambiguous
  names, conflicting requests and Lua errors refuse the batch. Lua chooses the
  names and synchronization policy. No Hold-specific names are in the backend.
* `vehicle_control_query` is a read phase: a Lua handler can return another
  enabled button's zero-based index, or nil to retain the original engine read.
  The SDK supplies a validated reference to that state, so the game's own query
  retains its arguments and return semantics in the same update. Writes are
  forbidden in this phase. The backend has no copy-selection policy. Conflicting
  aliases or Lua errors retain the original read. With no subscribed mod it
  skips snapshots and Lua entirely.

## Early engine decisions

Island controllers can be created inside server initialization, before the
normal session notification and first independent runtime update. A gameplay
mod can provide a separate content-relative entry in its manifest:

```json
{"id":"example.mod","scripts":["bridge/main.lua"],"rules":"bridge/rules.lua"}
```

The SDK loads `rules.lua` in a separate restricted VM only when the engine asks
for an early decision. That VM can register controller/spawn callbacks, require
its own safe helper modules, and use base/table/string/math/UTF-8/coroutine
functions. It has no world selectors, input, messages, native operations or OS
API. Its callbacks receive copied numeric arguments and return a decision.
Ordinary runtime modules report those two early events as unavailable.

Normal modules likewise have no `os`, `io`, `debug`, `package.loadlib`, arbitrary
`load`, `loadfile` or `dofile`. The Host's private source loader restricts
`require` to this module's validated code root and the shared SDK facade. This
does not make a gameplay feature multiplayer-compatible or prove all engine
operations harmless: mods still choose what to do with their verified API.

## Screen-to-module requests

Game-owned screen Lua and independent mod VMs do not share globals or pointers.
The screen calls `cc2x.modules.send(owner, topic, subject, value)`. The selected
module drains `cc2.messages.poll()` during a server callback. Each entry contains
`topic`, `subject`, `value`. The module validates its request before acting, and
can publish numeric state with `cc2.messages.publish(topic, subject, value)`.
Screens read this using `cc2x.modules.read(owner, topic, subject)`.
`cc2.messages.forget(subject)` deletes that module's state for a retired object.

Topics are 1..64 ASCII letters, digits, dots, underscores or hyphens. Subjects
are copied uint32 identifiers; values must be finite numbers. There are at most
64 queued requests and 256 published states per module. Full queues return
false. Only the owning module drains/publishes its endpoint; inactive owners
cannot receive requests. Sessions, reload, load failures and removal discard
the appropriate state. A successful send means queued, not applied. There is
no implicit multiplayer RPC or remote-client authentication in this channel.

## Mod structure

Keep `mod.xml`, preview, gameplay assets, interface Lua and `content/bridge.json`
in the gameplay package. Its bridge scripts contain policy and configuration.
The Lua package declares `lua_api="2"` in its cc2mce section, requires
`cc2x.runtime`, and Steam Required Items must point to the
SDK Workshop item. A gameplay package contains no `dinput8.dll` or private DLL.
Source code, native research and tests stay in the source repository. Separate
native-plugin packages remain supported through the C SDK and native packager;
they retain the ModLoader's exact-hash review requirement.

Mods consume this API as provided: a Lua author does not modify the SDK or
compile C; a native author uses the public C interface. Engine capabilities that
are missing are implemented and reviewed once, then exposed to both languages.
A per-mod switch around an
entire hard-coded native gameplay implementation does not meet this boundary.

## Verification and open acceptance

The fixture suite checks a zero-module Host with no gameplay writes, actual Lua
callbacks over a fake game, mailbox ownership/session/capacity/thread behavior,
generic throttle profile checks, and staged named-button transport. Separate
Lua policy fixtures exercise Modes and Hold. These are offline checks.

The empty-SDK, first-read alias and early construction-time rule boundaries
have offline regressions. Fresh in-game acceptance, Hold first-press timing,
remote multiplayer and dedicated-server behavior remain required. The full
Brake/Island gameplay ports are not complete yet.
