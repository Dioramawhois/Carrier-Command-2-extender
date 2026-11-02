# Source provenance and notices

The actual working source integration is included. Original notices and
attribution are retained.

| Component | Source revision | Existing notice |
|---|---|---|
| CC2X | Dioramawhois/Carrier-Command-2-extender, `7ab05c79357e0cdb5bfaa2866bd3a81a101a09da`, including the local changes | [GPL text](third_party/cc2x/LICENSE), [other notices](third_party/cc2x/licenses/) |
| Lua 5.3.6 | lua/lua, `75ea9ccbea7c4886f30da147fb67b693b2624c26` | [MIT notice](third_party/Lua-LICENSE.txt) |
| Zig 0.15.2 | Downloaded by the build script, outside the source archive | Notices in the downloaded distribution |
| Bridge and mod assets | Project sources and existing aircraft fixture assets | No new license is assigned by this source export; existing attribution is retained |

`third_party/SOURCE_MANIFEST.json` records vendored files and SHA-256 values.
`patches/cc2x-native-bridge.patch` preserves upstream changes for review. The
build uses the already modified sources and does not apply the patch.

The original Git history, compiler caches, game executable, crash dumps and
bulk REA exports are excluded. Historical documents may mention their locations
in the original workspace.
