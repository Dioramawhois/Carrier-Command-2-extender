# Carrier navigation: guarded research

The read-only `tools/verify_navigation_profile.py` checks the exact reviewed
Windows 1.5.18-r2 executable, PE identity, carrier vtable and instruction guards.
It does not load the executable, install a hook, change a control or enable a mod.

Confirmed from this profile: carrier motion slot 57 contains a native waypoint
branch for control mode 0 and physical helm handling for mode 1. The waypoint
branch calls the engine path follower, writing the native control target and
speed fields. The manual branch reads steering/throttle factors and the side
thruster switch. Presence of these branches does not establish that changing
control mode alone safely provides a human carrier autopilot.

Next native work must verify the normal mode transition, helm takeover,
authority, waypoint/path replication and save/load. The carrier motion branch
does not visibly use its speed output as a cruising-speed limit; do not claim
that writing `CC2X_VEHICLE_CONTROL_SPEED` implements speed selection. A generic
guarded speed/control interface still needs implementation and C/Lua parity.

Carrier Navigator owns route UX, requested speed and defensive policies. Those
rules do not belong in this SDK. The published 0.9.0-dev runtime is unchanged.
