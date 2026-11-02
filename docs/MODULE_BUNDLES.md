# Independent Lua modules in a merged game package

The unified runtime accepts the existing Bridge manifests unchanged: modern
`content/bridge.json`, legacy `content/native_bridge/bridge.json` or root
`bridge.json`. Each module retains its owner ID, isolated VM and relative code
root. Game-owned stock scripts are not allowed as independent entry points.

A packager can preserve multiple modules in one active game mod using
`content/cc2x-modules/index.json`:

```json
{"schema":1,"packages":["source-a","source-b"]}
```

The JSON contract is `sdk/schemas/module-bundle.schema.json`; filesystem,
case-insensitive identity and atomic validation are enforced by the runtime.

Each string identifies a child container under `content/cc2x-modules/`. A
container has its own `mod.xml` and original `content/` (or legacy root manifest
and Lua entry). The outer game mod must have `mod.xml` and be in the validated
active-package snapshot. Containers are never discovered independently from an
inactive outer mod. Do not put native plugins or another runtime inside them;
native activation still belongs to the reviewed outer package's `content/cc2x`.

The index is at most 64 KiB with at most 256 package identifiers. Keys are unique,
schema is exactly 1, identifiers use the same bounded ASCII ID grammar as module
IDs, and duplicates are case-insensitively rejected. Absolute paths, traversal,
missing packages, reparse points and nested indexes are rejected. A malformed
bundle contributes no Lua modules; there is no partial/fallback activation.
If any explicit index is used, discovery preserves active-package order and the
order of its entries. Without an index, legacy alphabetical-ID ordering stays
unchanged. Duplicate owner IDs across any active packages reject every colliding
module, without discarding unrelated modules.

The module index is runtime metadata, not a game data file to three-way merge.
The packager must preserve every module's files, validate Lua syntax, report
source/output hashes and require the exact reviewed runtime with bundle support.
The SDK reader alone does not add this behavior to an older ModLoader build.
Shared engine resources still need the normal ModLoader merge, independently of
private module paths. No save/network catalog migration or client delivery is
implied by packaging: host/client agreement and dedicated bootstrap remain open.
