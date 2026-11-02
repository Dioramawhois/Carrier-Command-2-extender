# CC2X independent Lua SDK

The independent Host automatically installs sdk/cc2.lua as cc2 and require('cc2')
in every active Bridge VM. Do not inject it into game screen templates.

See ../docs/INDEPENDENT_LUA.md for the independent facade and manifest layout,
and ../docs/LUA_API.md for the game's ordinary cc2x Lua API.
Only engine primitives belong in C++; gameplay decisions and key bindings stay Lua.
tools/build_unified.py generates the embedded copy from this single source file.
