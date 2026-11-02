# Integrated SDK test scope

`python build.py --zig PATH --test` is the current integrated suite. Its explicit
list in tools/build_unified.py builds 18 executables using production SDK code,
trusted SDK Lua wrappers and synthetic/fake-game fixtures. No actual mod Lua,
game DLL or author plugin DLL is executed by it.

Other tests imported from the original team snapshot are retained as source
evidence. Several describe old prototype-only contracts, an incompatibility
policy bypass or original standalone packaging, and are not integrated SDK
acceptance tests. Their old gameplay assumptions do not define this runtime.
Legacy headers and removed implementation are retained only for this review;
tools/build_unified.py never links them. Tests needing real EXE/assets or an
actual installed game require separate explicit execution and evidence.

The static `verify_new_build_profile.py` reads a provided legitimate executable
without running it. Python test_unified_distribution.py inspects the built PE
and rejects prototype/research/policy-bypass markers. Source extraction tests use
temporary synthetic files and preserve existing destinations.

Offline success is not fresh menu/gameplay, multiplayer or dedicated acceptance.
