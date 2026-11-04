# Brake v15 to Lua: what ports today, and the missing engine primitives

Status: proposal and engine notes. This change adds no code.
Author: Yolo (`totolerigolo118`), who wrote the native mod, with an AI assistant.
Audience: SDK maintainers and the coding agents working on this repository.

The research behind this note is in the public repository
`totolerigolo118/carrier-command-2-mods` at commit
`a40d1572eabd6d9710e6d64aae431824ffc98790`. That commit is already pinned in
`integrations/cc2mods-source-lock.json`. All paths below are relative to its
`tour/atterrissage_manuel/` folder. Addresses are image VAs, as given by `va()`.

The SDK rule applies throughout: the SDK provides checked engine mechanisms,
and the gameplay policy stays in the Lua mod. Nothing below asks the SDK to
know about "brakes", "manual landing" or any ini key.

## 0. Summary

| Native v15 feature | Lua API 2 today | Missing |
|---|---|---|
| Brake a hand-landed aircraft on its carrier, hold it when stopped, idle the engine when the pilot leaves | Yes (see §1) | Persistent settings (§4) |
| v15: hold a helicopter that its pilot left on the deck (power 0 plus brake) | Yes | Persistent settings |
| `auto_recall`: issue the carrier's own recall once the pilot has left | No | **Carrier dock request** (§2) |
| `recall_force`: make the carrier's landing manager take over an aircraft that already stands on the deck (hooks H1, H2, H5) | No | **Deck recovery gates** (§3) |
| `heli_recall`: the same for a helicopter (hook H6) | No | **Deck recovery gates** (§3) |

Without §3, a plane recalled while it stands on the deck stays queued. The
game never grants it the runway: its gate wants the plane less than 1000 m
from the approach gate. A helicopter is worse: the game makes it take off,
fly to its holding point about 260 m astern, and land again.

In-game evidence for the native version is single-player only. On 02/11, the
Albatross, Manta, Razorbill and Petrel all went back to the hangar by
themselves (v15 on the r2 exe). Multiplayer was never tested.

## 1. Part that ports now (Lua API 2)

Every input and output of the per-tick policy already exists. The source is
`frein/cc2x_frein.c` (`on_tick`, `heli_hold_tick`), with settings in
`frein/frein.ini.example`.

| Native behaviour (ini keys) | Lua API 2 |
|---|---|
| Own carrier = nearest vehicle of `definition` 0 (carrier) and the same team | `cc2.world.vehicles()` |
| Distance, height and speed relative to the carrier (`zone_m`, `height_m`, `max_speed`) | vehicle `position`, `velocity` |
| "Has flown": about 30 ticks with no wheel touching (`need_airborne`); `min_wheels` | selector `count(fields.CC2X_VEHICLE_WHEELS)`, `getf(fields.CC2X_VEHICLE_WHEEL_CONTACT, i)` (the game's own touchdown test uses ≥ 0.01) |
| Pilot present | `control_mode == 1` (the R key sets 0 while the pilot stays seated; the native mod treats that as "pilot gone") |
| Brake (`brake`, `grip`, `grace_s`, `flight_state`, `only_id`) | selector `brakes(brake, grip)`, every tick |
| `hold_brake`: stronger brake, latched below 1 m/s | same call, other values |
| `hold_idle`: engine to 0 once the pilot is gone, given back on release | selector `setf(fields.CC2X_VEHICLE_CONTROL_POWER, 0, 0)`, after saving the previous value |
| `recall_hold`: keep holding while `dock_state` is 5 (queued) | `dock_state` |
| v15 helicopter hold: all wheels touching, < 1 m/s, `dock_state` 0, pilot just left | same calls as above |

Porting notes:

- The `dt` passed to `on_update` is wall-clock time (`GetTickCount64`). The
  native mod counts ticks (30 per second). Players often run the game
  accelerated, so Lua timers should use `cc2.session.tick()` deltas.
- The native mod writes the brake and power after the game's tick
  (`on_server_tick`). The independent Lua host runs at the same point, so the
  timing is the same.
- Never set the position of a vehicle that stands on its wheels.

## 2. Missing primitive 1: carrier dock request

This is the engine call behind the map's "recall to carrier" order. It is
generic: it is what the server does when a player drags an aircraft onto a
friendly carrier.

- **Function:** the carrier's virtual function at vtable offset `+0x7c`.
  - 1.5.18-win32 (PE `0x6a36c177`): vtable `0xd596cc`, entry `0x707330`.
  - 1.5.18-win32-r2 (PE `0x6ae228f3`): vtable `0xd596ec`, entry `0x707440`.
  - The function bodies are identical instruction for instruction.
- **Convention:** `thiscall void (carrier, world, vehicle_id)`, where
  `world = server + 0x344` in both builds. This is the same vehicle map that
  the game's find-by-id uses (`0x7784e1` / `0x7785d1`).
- **Callers in the game:**
  - the player order handler `0x7784e9` → `0x5ebf80` ("requester is not a
    carrier and target is a carrier");
  - the AI's support-waypoint code `0x6c2bb7`.
- **Effect** (read in the exe; measured in game with v14):
  - if the aircraft is not already queued, it pushes `{id, 0, 0}` onto the
    carrier's landing queue at `[carrier+0xd3c]` (elements of 12 bytes:
    id, circuit state, field);
  - it sets `DOCK_STATE = 5` and `DOCK_VEHICLE = carrier`;
  - it does its own checks: already queued or already on a runway means no
    effect.
- **Guards used by the native mod** (`auto_recall_try`):
  - server thread only;
  - both objects found again by id on the same tick;
  - the carrier's vtable pointer must be exactly the expected one for the
    profile, and its entry `+0x7c` exactly the expected function;
  - at most one call per landing, decided by the policy.

The Lua equivalent of the map order would be the four screen calls
`clear_waypoints`, `clear_attack_target`, `add_waypoint`, `set_target_vehicle`.
Those exist only in game-owned screen VMs, which run only while that screen is
open. Independent modules have no "add waypoint". A checked dock request is
therefore the clean primitive.

## 3. Missing primitive 2: deck recovery gates

The carrier's landing manager can take over an aircraft that already stands
still on the deck. Its own touch test and take-over block accept such an
aircraft. Two distance gates stop it before it gets there. The native mod
skips those gates for one flagged vehicle only, and only by changing registers
or the jump target. It never writes game memory.

### 3.1 Sites

In the landing function, r2 = old + `0x110` (4775 instructions compared, no
difference).

| Hook | 1.5.18-win32 | 1.5.18-win32-r2 | Expected bytes | Role |
|---|---|---|---|---|
| H1 | `0x7018ce` | `0x7019de` | `8b 40 04 83 f8 05` (`mov eax,[eax+4] ; cmp eax,5`) | eax = queue element just fetched, edi = landing manager. For the flagged id, hand the game a private copy of the element with circuit state 1. The real element is never written. |
| H2 | `0x701a3d` → `0x701a8a` | `0x701b4d` → `0x701b9a` | `8d 85 28 fd ff ff` (`lea eax,[ebp-0x2d8]`) | Inside "give the runway", after the game checked `[edi]==1` (docking mode) and `[edi+0x5c]==-1` (runway free). Jump past the < 1000 m gate. The game then writes the runway owner and runway state 3, dequeues the aircraft and sets dock state 1 itself. The skipped code fills scratch locals only. |
| H5 | `0x6fe470` → `0x6ff20a` | `0x6fe580` → `0x6ff31a` | `c6 85 fb fd ff ff 01` (`mov byte [ebp-0x205],1`) | "Final zone" branch (< 200 m from the runway middle), which otherwise aborts an uncommitted approach. Set `edi=[ebp-0x204]` (manager) and `esi=[ebp-0x200]` (carrier), then jump to the touch test that the game's helicopters reach. |
| H6 | `0x6fdcdd` → `0x6fdcf2` | `0x6fdded` → `0x6fde02` | `f3 0f 59 d2 f3 0f 59 db` (`mulss xmm2,xmm2 ; mulss xmm3,xmm3`) | Helicopter runway grant (manager mode 1, runway free, first helicopter of the queue, block `0x6fdba7`). Skip the "within 100 m of holding point (x −100, z −240, carrier frame)" test and go to the grant. xmm2/xmm3 are rewritten before being read. |
| H3 (diagnostic) | `0x6ff0fb` | `0x6ff20b` | `8b 8d 04 fe ff ff` | Read only: first instruction of "approach failed". Optional. |
| Take-over probe (diagnostic) | `0x6ff379` | `0x6ff489` | `89 47 6c 8b 8e 88 00 00 00` | Read only: the bay the take-over got. Optional. |

### 3.2 Layouts read by the hooks (both builds)

- **Landing manager:** `+0x00` mode (1 = docking), `+0x1c` runway state
  (3 = landing, 5 = taken over), `+0x58` lift owner, `+0x5c` runway owner
  (−1 = free), `+0xd4` approach counter (> 3600 = fail).
- **Carrier:**
  - `+0x154` bays (0x70 bytes each, occupant at `+0x64`), `+0x158` bay count;
  - `+0x88` model: node table at `+0xe0` (64-byte entries), `+0xe4` count,
    `+0xe8` first index, class at `+0x3c`, limit at `+0xec`.
  - A bay is free for class 1 exactly as the game's own search `0x5ec1e0`
    decides (`bay_free()` in `cc2x_frein.c`).
- **Aircraft:** `+0x188` preferred bay (byte), `+0xac` wheels (0x70 each;
  `+0` contact, `+4` second field), `+0xb0` wheel count.
- **Frame of the landing function:**
  - `[ebp-0x1fc]` aircraft, `[ebp-0x200]` carrier, `[ebp-0x204]` manager,
    `[ebp-0x205]` zone byte;
  - H6 block: `[ebp-0x270]` helicopter, `[ebp-0x278]` queue index;
  - speed vectors subtracted by the touch test:
    P = `[ebp-0x2e4]`, `[ebp-0x234]`, `[ebp-0x23c]` and
    Q = `[ebp-0x2dc]`, `[ebp-0x2d4]`, `[ebp-0x2a4]`.
- **After the jump, the game's own code decides:**
  - touch test: relative speed ≤ 10 m/s, every wheel with contact ≥ 0.01 and
    second field ≤ 0.1;
  - take-over: bay, joint, `DOCK_STATE` 6, `FLIGHT_STATE` 1, runway state 5,
    push-back to the lift;
  - abort path `0x6ff0f1`: brake 0, power 1, gear up, `DOCK_STATE` 5,
    re-queued.

### 3.3 Safety rules of the native version (keep them in the primitive)

- **One armed vehicle at a time.** The hooks compare the game's pointer or id
  with the armed one, and do nothing for anything else.
- **Budgets counted inside the hooks, per window:**
  - H2: at most `force_tries` (default 3) grants;
  - H5: at most 30 jumps;
  - H6: one grant;
  - window timeout: 20 s for planes, 10 s for helicopters.
- **Loop breaker:** after a grant, if the element comes back with circuit
  state 3 (the game aborted), H1 does not fake it again.
- **H2** acts only if H1 flagged the same element in the same iteration, and
  only if `[edi]==1` and `[edi+0x5c]==-1`.
- **H5** acts only if all of these hold: `edi` is the armed aircraft, runway
  state is 3, runway owner is the armed id, at least one free bay of class 1
  exists, and the relative speed from P−Q is ≤ 2 m/s.
- **H6** acts only if all of these hold: `[ebp-0x270]` is the armed
  helicopter, `edi == [ebp-0x204]`, mode is 1, owner is −1, and a free bay of
  class 1 exists.
- **Registers:** the native mod saves xmm0–7 around every hook body
  (`XMM_WRAP`). Check whether the current `patch_mid` relay already does.
- **All-or-nothing install:** the hooks go in one transaction, are installed
  only when first requested, and verify their exact bytes first. They are
  refused if another plugin owns a site, for example the legacy
  `cc2x_frein.dll`.

### 3.4 Proposed shape (names are suggestions)

A native flag, armed by the policy, is preferable to a Lua callback inside the
hooks. The hooks run inside the landing loop for each queue element. A native
comparison keeps Lua out of that hot path, and the budgets still hold if the
Lua handler fails.

**Lua** (capability `carrier-recovery-v1`):

```lua
cc2.carrier.recovery_available()                    -- profile supports it, sites free
cc2.carrier.dock_request(carrier_id, vehicle_id)    -- queued; runs next server tick
cc2.carrier.recovery_arm(carrier_id, vehicle_id,
    {window_ticks = 600, grants = 3, touch_jumps = 30})
cc2.carrier.recovery_disarm(vehicle_id)
cc2.carrier.recovery_status(vehicle_id)             -- {armed, grants, jumps, last_refusal}
```

**C:** the same operations in the named framework extension, on the same
backend.

The policy stays in the mod: when to arm (for example "recalled while held,
stopped, pilot gone N ticks"), which teams or ids, the auto-recall delay, and
retries. The backend must refuse:

- vehicles that are not aircraft;
- carriers of another team, if you want that rule in the SDK rather than the
  mod;
- stale sessions;
- a second armed vehicle.

## 4. Missing primitive 3: persistent per-module settings

Independent modules cannot read any file, so a Lua port has no `frein.ini`
(about 20 keys) and no `modes.ini`. The native mod re-reads its ini every
90 ticks, so players can tune values during a game. Two options:

- **A read-only `cc2.settings` facade:**
  - typed values from `%APPDATA%\Carrier Command 2\cc2x\<module_id>.ini`;
  - flat `key=value` pairs, numbers and booleans only, bounded size and key
    count;
  - `get(key, default, min, max)` and `reload()`;
  - no path or handle ever reaches Lua.
- **ModLoader-managed settings**, exposed through the same read-only facade.
  CC2-ModLoader's ADR-0018 ("reviewed SDK setting defaults") may already
  cover part of this; it was not checked for this note.

Until this exists, a Lua port can only keep constants at the top of
`main.lua`, and Steam replaces those on every mod update.

## 5. Tests and acceptance suggested

- **Paired C/Lua fixtures** for `dock_request`:
  - refused if the vtable or entry differs;
  - refused if the session is stale;
  - one call per request;
  - `dock_state` read back.
- **Hook fixtures:**
  - exact bytes for both profiles;
  - nothing happens for a non-armed id;
  - budgets are exhausted correctly;
  - the loop breaker works;
  - no write to fake game memory;
  - xmm registers are preserved;
  - the transaction is refused when a site is taken.
- **Native test material** is in the public commit above:
  - `frein/tests/test_frein.c`: 470 host checks for v15 (the 45 injected
    faults it catches are listed in `frein/RISQUES_v15.md`);
  - `frein/VERIF_sites_v12.txt`, `VERIF_sites_v13.txt`, `VERIF_sites_v14.txt`
    and `outils/verifier_sites_v13.py`: static site checks on both exes.
- **In-game acceptance** (single-player first): plane and helicopter, each in
  three cases:
  - recall from the map while held;
  - auto-recall after the pilot leaves;
  - no free bay (must stay held and log a refusal).
- **Multiplayer:** host only. Clients are untested.

## 6. Notes for the Hold and Modes ports

- **Hold:** the legacy `cc2x_hold.dll` patches `0x6fcd62` and `0x6fd93c`
  (r2: `0x6fce72` and `0x6fda4c`), the same sites as `vehicle_control_query`.
  While it is installed, the Lua subscription is refused, as designed. Player
  instructions for the Lua Hold should say to remove the old DLL.
- **Modes:** the deployed UI
  `tour/etudes_modes_carburant/lua/v2_1/library_custom_5.lua` (SPEED tab)
  calls `cc2x.modes.set/get/hold/status`, which only the native plugin
  registers. A Lua Modes needs that tab moved to `cc2x.modules.send/read`.
  Its `modes.ini` has the same settings gap as §4.

## 7. Source map (commit a40d157, `tour/atterrissage_manuel/`)

- `frein/cc2x_frein.c`: v15. Site table `k_builds`, hooks `hook_h1_body` to
  `hook_h6_body`, `install_hooks`, `auto_recall_try`.
- `CONCEPTION_retour_auto.md`: how the recall order reaches `+0x7c`.
- `ETUDE_v13_helicos.md`: the helicopter path, step by step.
- `PISTE_v12_comme_un_helico.md` and `PLAN_prise_en_charge.md`: the plane path
  (H1, H2, H5).
- `frein/RISQUES_v12.md` to `frein/RISQUES_v15.md`: what each version changed
  and what was proven.

These files are in French. The C comments are in English.
