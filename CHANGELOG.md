# Changelog

## 0.3.0 (2026-10-25)

- Plugins come with their mods: CC2X reads the game's list of enabled mods (`mods.xml`, local and
  Steam Workshop mods alike) and loads `<mod>\content\cc2x\*.dll` of each (inside `content`, which is
  what the game uploads to the Workshop). Disabling a mod in the game's mod
  menu disables its plugin. A plugin file name is loaded once; an enabled mod wins over an old copy
  in the game folder (logged).
- API 2: `plugin_dir()` and `config_dir` (`%APPDATA%\Carrier Command 2\cc2x`).
- Release = the bare `dinput8.dll` plus readme and SHA-256. Licence GPL-3.0-or-later.

## 0.2.0

- One `dinput8.dll` in the game folder: build check, transactional patching, server and session
  events, world snapshots, `cc2x` table in the game's Lua, plugins `cc2x_*.dll` from the game folder.
