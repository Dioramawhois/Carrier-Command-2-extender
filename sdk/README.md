# CC2X independent Lua SDK

Native C/C++ and Lua are supported SDK development routes. The
[C/Lua capability map](../docs/C_LUA_PARITY.md) lists their shared engine
operations, examples, tests and remaining migration gaps. New native consumers
use `include/cc2x_framework.h` alongside the stable `cc2x.h` prefix.

CC2X 0.7.0-client-beta.1 provides **Lua API 2**. See
[framework Lua modding](../docs/FRAMEWORK_LUA_MODS.md) for engine events, read-only
early rules, named-button operations, wing throttle, the UI request/state
channel, package layout and capability requirements. Authors write Lua/content;
they do not need a C compiler or changes to the SDK core. Gameplay behavior and
interfaces stay in separate mods. The older 0.6 client beta does not
implement these new calls; do not label a new mod compatible with that binary.

The 0.7.1 client candidate also supports optional package-owned
[screen extensions](../docs/SCREEN_EXTENSIONS.md), including stock menus.
This package contract is separate from the independent Lua API and adds no
default screen policy to the SDK.

The independent Host automatically installs sdk/cc2.lua as cc2 and require('cc2')
in every active Bridge VM. Do not inject it into game screen templates.

See ../docs/INDEPENDENT_LUA.md for the independent facade and manifest layout,
and ../docs/LUA_API.md for the game's ordinary cc2x Lua API.
Only shared engine primitives belong in the SDK core. Gameplay decisions and
key bindings stay in separate Lua or C/C++ plugins. Both development routes
must expose the same engine capabilities; the supplied Lua mods need no C build.
tools/build_unified.py generates the embedded copy from this single source file.
