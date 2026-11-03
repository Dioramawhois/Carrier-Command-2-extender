# Private game runtime tests

`tools/runtime_sandbox.py` is a developer fixture, not a player installation
route. It stages a separate byte-identical legitimate client executable, copied
stock resources and a private user profile. It refuses an existing destination,
unknown executable, native-containing consumer package or mismatched SDK evidence.
It neither changes the installed game nor uses a live production save in place.

The game obtains APPDATA through `SHGetFolderPathA`, so changing the environment
alone is insufficient. `tests/runtime/isolation_proxy.c` is an explicitly
test-only early import adapter. In the private copy only, its generated Steam
export relay forwards to a byte-identical copy of the installed Steam DLL. The
pinned x86 MinGW linker prefixes the relay module with an underscore; that exact
private filename is used and an actual forwarded export is resolved by the probe.
No Steam API implementation or proprietary library enters the SDK/player package.

Before copying/running the real executable, the fixture compiles and runs its own
small executable with the actual Windows folder import. The probe must return
the private profile and resolve the forwarding export successfully. The adapter
also requires explicit environment and sandbox markers and the expected private
`game`/`profile` relationship. Failure aborts loading before game entry.

The copied game's executable bytes stay unchanged. Only its in-process APPDATA
import is redirected. The SDK production DLL has no test activation, filesystem
redirect or selected gameplay mod built into it. The adapter is never linked by
the production build and must never be installed into the ordinary game folder.

Staging records before-hashes of the normal user files; tests compare those
afterwards and never restore a file over an independent user/other-chat change.
Test inputs and runtime logs must identify the sandbox, exact SDK hash and any
test-only Lua screen/driver additions. A modified test package is not silently
advertised as acceptance of an unmodified public player package.

Example developer staging (paths are explicit; nothing is launched):

```text
python tools/runtime_sandbox.py --game GAME --mod LUA-PACKAGE --zig ZIG --sandbox NEW-PRIVATE-FOLDER
```

Game launch/input automation must target only the recorded private executable and
PID. Close that process before replacing any of its test binaries or scripts.
Keep each run's log before the runtime's next startup replaces `cc2x.log`.
Single-computer host checks, copied-script tests and the isolation probe do not
certify a second licensed remote client, a dedicated profile or ordinary install.
