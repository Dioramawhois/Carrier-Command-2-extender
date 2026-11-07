# SDK update notifications

The official SDK Workshop package carries an owned append-only main-menu
extension. Packaging generates its available version from CC2X_VERSION. The
extension compares that with `cc2x.version`, which describes the loaded DLL.
It does not confuse the downloaded Workshop DLL with the installed runtime.

An older or unknown loaded version shows a large dialog on the main menu.
HOW TO UPDATE explains closing the game, using CC2 ModLoader beta 12 or newer,
and the alternative manual copy. CONTINUE WITH OLD SDK dismisses the dialog for
that menu VM, retaining a visible update banner and an instructions button.
Current/newer runtimes leave callbacks untouched. Input is routed exclusively
to the notice while it is open; the original menu is retained underneath.

The notification is shipped as Lua outside the new DLL so SDK 0.10 and other
screen-extension-capable runtimes can display it. The SDK package must be active.
Absent DLLs or runtimes predating screen extensions cannot receive this UI.
Workshop supplies the update metadata offline; no game-thread HTTP, executable
launch, live DLL replacement or native self-updater is introduced. ModLoader
continues to verify signed runtime updates and installs only with the game closed.

This notice recommends updating. Mod-specific mandatory capability failures
remain the responsibility of the affected feature; compatible native Navigator
behavior stays usable on SDK 0.10. Main-menu and owned Lua fixtures are kept
separate from gameplay evidence.
