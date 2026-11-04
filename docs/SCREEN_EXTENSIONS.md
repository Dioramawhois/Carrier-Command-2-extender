# Optional screen extensions

The current unified runtime is **0.8.1-client-beta.1**. It includes both this
mechanism and the 0.8 waypoint-origin/session-safety capabilities.

CC2X 0.7.1-client-beta.1 adds `screen-extensions-v1` on the reviewed Windows
1.5.18-win32-r2 profile. It lets an enabled package append its own Lua code to
an exact game screen, after the stock screen chunks execute and before `begin`.
This also reaches stock menu screens whose resource reads bypass ordinary mod
content. The SDK ships no menu policy, gameplay feature or enabled extension.

Add this file to an ordinary local or Workshop package:

```text
mod.xml
content/
  cc2x/
    screen_extensions.json
    ui/
      example.lua
```

`content/cc2x/screen_extensions.json`:

```json
{
  "schema": 1,
  "extensions": [
    {"screen": "scripts/screen_menu_main.lua", "source": "cc2x/ui/example.lua"}
  ]
}
```

Both paths use forward slashes and are relative to `content`. The `screen`
name must match the engine's script name exactly; suffixes, case variants and
wildcards do not match. Sources must stay under `cc2x/ui/`. Use ASCII path
components with letters, digits, `_`, `-` and `.`; empty components, `.`/`..`,
absolute paths, backslashes, controls and alternate data streams are rejected.
Source code may contain UTF-8 text.

An extension runs in the **game screen VM**, with the same engine API as that
screen. It is not an independent Bridge module and needs no `bridge.json`.
Keep the original callbacks when wrapping them. Ordinary screen API and thread
rules still apply: do not access server objects directly from a UI callback.
Check `cc2x.has("screen_extensions_v1")` when detecting support in a screen VM.

Only enabled packages from the core activation snapshot are considered. The
runtime snapshots and validates sources once at the first screen compilation,
after plugin enumeration completed. Restart the game after changing a source,
descriptor or selected set. Source order follows enabled package order and
descriptor order. Each declaration is appended once to its matching screen's
chunk list; it must not depend on another screen's globals.

The descriptor is limited to 64 KiB and 16 entries per package. The activation
snapshot is limited to 256 packages, with 128 extensions and 4 MiB of source in
total. Each nonempty UTF-8 source is limited to 256 KiB and compiled as text with
private Lua 5.3 under an 8 MiB allocation budget **without executing it** before
acceptance. Native/binary Lua chunks and syntax errors are rejected. File and
directory reparse points, directories masquerading as source files and resolved
paths escaping the declared content root are rejected. Runtime syntax checks
are a load check, not a security sandbox for enabled screen code.

An invalid descriptor or source rejects all extensions from that package and
adds `SCREEN_EXTENSION_REJECTED` to `cc2x.log`. Other valid packages remain
eligible. No extension means the ordinary stock screens remain unchanged; this
works with zero registered custom vehicles. The existing reviewed screen hook
is shared with the vehicle UI shim, so no overlapping second hook is needed.

`SCREEN_EXTENSIONS_READY` records snapshot counts.
`SCREEN_EXTENSION_SOURCE_QUEUED` identifies the matching screen, source path
and byte count. Queuing proves delivery to the Lua chunk list, not successful
execution or user interaction; authors should log an explicit initialization
event with `cc2x.log` and test their behavior in the game.

The JSON schema is in `sdk/schemas/screen_extensions.schema.json`; runtime
validation additionally enforces unique keys, literal schema version `1`,
unique screen targets within each package, real filesystem boundaries and Lua
syntax. Native client and Lua screen authors use the same package contract.
The total source-read budget also counts rejected packages, keeping startup
validation bounded when a selected package contains malformed code.
Other game profiles, native macOS and multiplayer behavior require separate
verification.
