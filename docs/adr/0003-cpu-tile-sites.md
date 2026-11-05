# ADR 0003: map sites before simulation streaming

Status: candidate; fresh runtime acceptance pending.

Island Airfields' first 2.2 candidate displayed an airfield only after approach
streamed the island. An island-centre fallback would misrepresent its location.

Expose a generic CPU placement preview shared by C and independent Lua. Use the
stock terrain task on private native allocations with exact profile/entry
guards, bounded synchronous discovery and session caching. Preserve simulation
streaming and return copied matrices instead of engine pointers. The gameplay
mod requests owned islands and publishes map coordinates through its existing
host-local mailbox; changing SDK prefixes or installing a private mod DLL is
unnecessary.

Cold generation cost and agreement with streamed placements are explicit
acceptance gates. No placeholder island-hub coordinate is treated as an airfield.
