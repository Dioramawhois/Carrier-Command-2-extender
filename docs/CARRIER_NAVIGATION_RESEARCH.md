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

The unpublished branch now implements the separately named
[motion interface](CARRIER_MOTION.md), including guarded native transitions,
manual helm/throttle priority and local ownership checks. Native slot 57 takes
seven stack words, confirmed by the `ret 0x1c` guard and hook ABI fixture.
The carrier branch does not use its control-speed field as a cruising limit;
selection caps the existing forward output instead. Full power has no knot cap.

Carrier Navigator owns route UX, requested speed and defensive policies. Those
rules do not belong in this SDK. The published 0.9.0-dev runtime is unchanged.
