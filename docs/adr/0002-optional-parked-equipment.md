# ADR 0002: optional shared parked-vehicle equipment

Status: implemented candidate; runtime acceptance pending.

Island Airfields needs free weapon changes in four island hangar bays. The stock
Lua setter addresses an aircraft through a carrier bay, so it cannot represent
this operation without falsifying carrier state.

Expose a separate named C interface and matching independent Lua facade. Reuse
the checked native attachment setter and its stock notification sequence. Keep
the existing C prefix, framework and Lua versions unchanged. The mod checks
ownership, bay membership and queues; the SDK checks live attachment bounds,
compatibility, stationary docked state and executable provenance. Neither layer
performs inventory accounting in this candidate.

UI requests use the existing host-local mailbox. Remote UI mutation is disabled
until a separately authenticated route and fresh multiplayer acceptance exist.
No private DLL is shipped by the gameplay addon. A future paid service must
introduce explicit inventory accounting instead of silently changing this free
primitive's contract.
