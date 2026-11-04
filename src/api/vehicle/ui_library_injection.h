#pragma once

struct cc2x_api;

namespace cc2::api::vehicle {

// Adds optional package-owned sources and the generic vehicle UI shim after
// stock screen chunks, before CC2 creates/executes Lua. No default menu policy.
bool InstallStockUiLibraryInjection(const cc2x_api* api);

}  // namespace cc2::api::vehicle
