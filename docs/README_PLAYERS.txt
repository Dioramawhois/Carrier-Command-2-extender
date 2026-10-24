CC2X - mod loader for Carrier Command 2 (Windows, game version 1.5.18)
=======================================================================

Some mods need CC2X to do more than the game allows (for example Island Airfields).
CC2X is one file: dinput8.dll. You install it once.

INSTALL

1. Download dinput8.dll.
2. Steam: right-click Carrier Command 2 > Manage > Browse local files.
3. Put dinput8.dll into that folder (next to carrier_command.exe).

That's all. Mods that use CC2X come from the Steam Workshop like any other mod: subscribe,
enable them in the game's Mods menu, restart the game.

If that folder already has a dinput8.dll, it belongs to another mod: keep a copy of it,
the two cannot be used together.

REMOVE

Delete dinput8.dll from the game folder.

GOOD TO KNOW

- CC2X changes nothing on disk: it only patches the running game in memory.
- After a game update CC2X switches itself off until it is updated; the game runs normally.
- cc2x.log in the game folder lists what was loaded. Attach it when reporting a problem.
- Mods with CC2X plugins run their own code on your PC, like any mod loader. Use mods you trust;
  all CC2X mods have to publish their source code (GPL).
- Not supported: VR, dedicated server, macOS.
