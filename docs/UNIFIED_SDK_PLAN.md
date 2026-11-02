# Unified SDK / separate gameplay projects

Owner decision, 2026-11-02: evolve this existing CC2X runtime, not a new SDK or
parallel dinput8 provider. Keep runtime identity cc2x.runtime and gameplay out
of the core. The full Russian integration map is in CC2 ModLoader
docs/22-UNIFIED-SDK-AND-MOD-SPLIT.md (companion MR).

The owner confirmed all team permissions for integration and publication under
the project's GPL-3.0-or-later license. See NOTICE.md and the archived snapshot
notice. The per-mod Lua host, generic registries and UI/resource operations are
now integrated into one runtime, built by the ordinary build.py entry point.
There is one C core, not a vendored second CC2X. Canonical cc2x_api is team API 8
with the original API 4 prefix retained (CC2X_API_V4_SIZE). The independently named
session extension remains separate. Old draft API 5 session-tail binaries must
be rebuilt; numeric version alone never proves fork-layout compatibility.

Aircraft, Hold copies, ECO/FLANK, manual braking and Island Airfields are separate
consumers. Generic engine operations belong here; hotkeys, tuning and policy do
not. Lua gameplay migration is incremental after real public primitives exist.

## Reproducible cc2mods source extraction

`python tools/extract_cc2mods.py --source PATH --output NEW_DIRECTORY` reads the
pinned upstream commit and SHA-locked list in integrations/cc2mods-source-lock.json.
It validates all source bytes before writing, rejects unsafe paths and existing
outputs, preserves GPL/attribution and produces three separate source projects.
Each build adapter takes --sdk/CC2X_SDK_DIR and --zig. It builds only its plugin,
records source/SDK/compiler/artifact hashes, never installs or executes it.

Upstream C/tests are kept byte-identical. UI generators are reference sources;
finished UI/resource packaging and live supported-profile tests remain pending.
No Workshop IDs, mod.xml or game assets are invented/copied into this extraction.
The outputs are developer sources, not complete player mods. Tests:
`python tests/test_source_extraction.py` checks modified upstream rejection before
write, unsafe paths, preservation of existing outputs and exact valid copying.

## Required integration contracts

- Preserve separate Lua code roots and manifests inside ModLoader's single
  generated game package. An ordinary merge of bridge.json/main.lua loses mods.
- Preserve original owner IDs; execute modules in declared dependency/user order.
- Stable definition mappings/catalog fingerprints for save and network sessions.
- Host authority and real client replication, with dedicated profiles/bootstrap.
- Custom types may require native runtime/catalog on clients; Workshop content
  delivery alone does not install their DLL or certify multiplayer.
- Supported game profiles, common build toolchain, notices and fresh native tests
  before any public combined runtime or trust-profile update.

## Current evidence and remaining gates

`python build.py --zig PATH --test` builds the unified x86 DLL using only pinned
Zig 0.15.2 and runs 18 native/SDK checks. They exercise actual SDK code with fake
game APIs and synthetic modules, trusted SDK Lua wrappers, session isolation and
native SEH fault recovery. They do not execute actual author mods or load the DLL
into a game. The production-artifact test rejects embedded aircraft scenario
names, research activation and the incompatibility auto-disable bypass marker.

The runtime now reads bounded ordered module bundles; see MODULE_BUNDLES.md.
Two modules with the same entry filename retain independent roots/owner IDs;
reorder, duplicate IDs, unsafe/missing/nested/oversize metadata are covered.
ModLoader's released beta still needs container publishing and an exact reviewed
runtime capability profile before its single generated folder can use this.
This gap is not hidden by merging the module manifests as ordinary JSON.

A read-only profile check passed against the locally installed Steam 1.5.18-r2
EXE: SHA/PE identity, lifecycle/throttle, Lua ABI, World API, all 26 animation
sites, hangar native entries and UI injection helpers. This is static evidence.
The new DLL has not replaced the installed runtime. Fresh in-game acceptance,
host/client replication, save/catalog migration and dedicated support remain
unverified. No new public binary release or native trust hash is implied.
