#pragma once

struct cc2x_api;

namespace cc2::api::vehicle {

// Adds cc2x.vehicles.definitions() to every stock game Lua state.
bool RegisterStockLuaApi(const cc2x_api* api);
// Install narrow process-wide helpers consumed by stock Lua UI extensions.
// These are optional so an older append-only CC2X API can still expose the
// namespaced vehicle metadata query above.
bool RegisterStockLuaCompatibilityApi(const cc2x_api* api);

}  // namespace cc2::api::vehicle
