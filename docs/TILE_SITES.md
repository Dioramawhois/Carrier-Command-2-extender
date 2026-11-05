# Optional CPU tile placement preview

Unreleased development interface: named C `cc2x_query_tile_sites_v1` and
independent Lua `cc2.tiles.sites(tile_id, kind, definition)`. Existing C API 8,
framework 1 and Lua API 2 retain their layouts.

The ordinary airfield snapshot only contains streamed simulation islands.
This query runs the stock server's CPU terrain load task on a privately owned
tile result using the live tile's seed, bounds, biome, difficulty and world
transform. It reads the placed environment objects and destroys the private
result through the stock task destructor. It does not attach an island to the
server tile or register simulation entities. World island/render pointers are
checked before and after generation.

Results are cached for the session and exact `(tile, kind, definition)` selector.
At most one uncached query may run per server tick; there are 128 cached queries
and at most 64 returned sites per query. The first query is synchronous and can
take time; mods should spread discovery over updates. A native fault disables
further previews for that session. The exact Steam r2 executable and the five
native constructor/allocator/run/destructor entries are authenticated.

Lua returns `nil, reason` on refusal or an array of `{transform, x, y, z,
flags_low, flags_high}`. Matrices are the game's 12-double world transforms.
Native clients use `include/cc2x_tile_sites.h`; output is not partially written
when capacity is insufficient. The count reports required capacity in that case.

The stock airfield builder selects environment kind 0, definition 3 and any
active layer among bits 0..7 of `flags_low`. This selector and the placement
matrix copy were checked against the installed executable. The API is generic;
island ownership and which sites to display remain mod policy.

Offline coverage exercises the production call pipeline with owned native
substitutes, input forwarding, moved ownership, site selection, overflow/NaN
refusal and destruction, plus C/Lua argument and unsupported-profile handling.
Actual CPU generation, alignment with streamed airfields, startup cost and
visual appearance require fresh in-game acceptance. No release claim follows
from the substitutes or static executable inspection.
