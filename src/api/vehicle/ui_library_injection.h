#pragma once

struct cc2x_api;

namespace cc2::api::vehicle {

// Adds the core-owned UI shim to each stock screen Lua chunk list after the
// stock screen script has been copied, but before CC2 creates/executes Lua.
bool InstallStockUiLibraryInjection(const cc2x_api* api);

}  // namespace cc2::api::vehicle
