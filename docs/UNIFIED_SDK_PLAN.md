# Unified SDK / separate gameplay projects

Owner decision, 2026-11-02: evolve this existing CC2X runtime, not a new SDK or
parallel dinput8 provider. Keep runtime identity cc2x.runtime and gameplay out
of the core. The full Russian integration map is in CC2 ModLoader
docs/22-UNIFIED-SDK-AND-MOD-SPLIT.md (companion MR).

Native Bridge's per-mod Lua host, generic vehicle registry and UI/resource
capabilities are candidates for this SDK after exact license terms are recorded.
The owner reports permission and will clarify terms. No proprietary/unspecified
Bridge code or aircraft assets are copied here. Do not vendor a second CC2X core.
Preserve the API 4 prefix and use checked named capabilities; do not reinterpret
the incompatible API 5-8 tails. Source adapters will be needed, not an untested
binary compatibility promise.

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
