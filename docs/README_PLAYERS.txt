CC2X - extender for Carrier Command 2 (Windows, game version 1.5.18)
=====================================================================

Install:   copy dinput8.dll into the game folder (the one with carrier_command.exe).
           Steam: right click the game > Manage > Browse local files.
Uninstall: delete dinput8.dll (and any cc2x_*.dll files) from that folder.

CC2X does nothing on its own. It lets mods do more: Steam Workshop mods that support it get extra
functions, and plugin files named cc2x_<something>.dll placed next to it are loaded automatically.

Log: cc2x.log in the game folder (written fresh at every start).

Safety: CC2X only patches the game in memory. Game files are never changed. If the game version is
not supported (for example after a game update), CC2X writes this into cc2x.log and stays inactive:
the game runs normally, mods just lose their extra features.

Not supported: VR version, dedicated server, macOS. Multiplayer: only the host matters.
Other mods that also use a dinput8.dll (e.g. ASI loaders) cannot be installed at the same time.
