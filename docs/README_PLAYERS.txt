CC2X SDK - Shared Runtime for Carrier Command 2
0.11.0-dev / C API 8 / framework C API 1 / independent Lua API 2
=======================================================================

CC2X supplies common native engine access and Lua support for compatible mods.
Install the SDK once; gameplay mods and their content are supplied separately.
The SDK itself adds no aircraft, flight modes, brakes, button layouts or island rules.

SDK UPDATE NOTICE

An older loaded DLL shows an update dialog in the main menu. Steam refreshes
the download, not the installed DLL. Close the game and update with ModLoader,
or follow the manual steps below. Compatible existing features remain usable.

INSTALL WITH A MATCHING CC2 MODLOADER

1. Subscribe to CC2X SDK and the compatible mods you want:
   https://steamcommunity.com/sharedfiles/filedetails/?id=3833014670
   Wait for Steam to finish downloading them.
2. Place CC2ModLoader.exe next to carrier_command.exe in the game folder.
3. Open ModLoader, enable CC2X SDK in Libraries, and select your mods.
4. Wait until the selected set is ready, then click Launch.

CC2 ModLoader beta 12 and later verify official signed SDK updates. Compatible
SDK updates install from the downloaded library without replacing the ModLoader
EXE. Existing runtime backups and Undo remain available. Older applications
need this one update to enable the mechanism.
ModLoader releases:
https://github.com/Dioramawhois/CC2-ModLoader/releases

INSTALL MANUALLY

1. Close the game. Download dinput8.dll from this exact client beta:
   https://github.com/Dioramawhois/Carrier-Command-2-extender/releases/download/0.11.0-dev/dinput8.dll
   Or copy it from the SDK's downloaded Workshop folder inside your Steam library:
   steamapps\workshop\content\1489630\3833014670\content\dinput8.dll
2. In Steam, right-click Carrier Command 2 > Manage > Browse local files.
3. Copy the DLL itself into that folder, next to carrier_command.exe:
   steamapps\common\Carrier Command 2\dinput8.dll
4. Start the game normally. Install and enable gameplay mods according to their
   own instructions; installing the SDK does not merge conflicting mod files.

If dinput8.dll already exists, identify it and keep a backup before replacing
it. Do not overwrite an unrelated loader without checking compatibility.
A Workshop subscription downloads files; it does not install the bootstrap.
After a Workshop SDK update, repeat the manual copy while the game is closed.

REMOVE A MANUAL INSTALLATION

Close the game, remove the CC2X dinput8.dll, or restore your previous loader
backup. Disable gameplay mods that require CC2X.

CURRENT SUPPORT AND REPORTING PROBLEMS

This development preview targets the reviewed Windows Carrier Command 2 1.5.18-r2 build.
Release assets identify the exact source commit, DLL hashes, offline test
evidence and separately recorded runtime acceptance in RELEASE.json.
Offline fixtures do not prove full gameplay compatibility.

Remote multiplayer, dedicated servers, changed-catalog saves, full custom-aircraft
flight/island landing and native macOS support remain unverified.
The owner accepted Carrier: Autopilot Waypoints alpha.4 navigation with Revolution
in the ordinary Steam installation through ModLoader after the helm-factor,
continuous turning-power and route-repeat corrections. The exact tested DLL is
preserved with its original build record and hash-verified source snapshot.
Separate live combat/countermeasure expenditure, exhaustive save/load and
clean-machine acceptance remain unverified. Carrier motion refuses multi-peer play.
The prior 0.9 preview received Island Airfields equipment and three-arrival
save/reload acceptance; that is historical evidence, not a new 0.10 gameplay test.
Full Brake behavior remains separate and unverified. The CPU tile preview refuses
unavailable tile definitions; do not rely on it for distant map positions.
The runtime is shared infrastructure, not a guarantee that every mod combination
will work together. ModLoader reports overlapping changes when preparing mods.

CC2X writes startup and plugin information to cc2x.log beside the game executable.
For bug reports include that log, game/SDK/ModLoader versions, enabled mods and
load order, and the steps needed to reproduce the problem.

SOURCE AND LICENSE

CC2X is open source under GPL-3.0-or-later. Preserve the bundled LICENSE and
third-party notices when redistributing the runtime.
Exact compiled source and hashes: RELEASE.json and dinput8.build.json.
Source and pinned compiler/library provider sources are separate GitHub release
assets. They are not included in the Workshop payload.
https://github.com/Dioramawhois/Carrier-Command-2-extender
