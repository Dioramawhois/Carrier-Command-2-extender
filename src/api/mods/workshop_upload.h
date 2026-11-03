#pragma once

#include <cc2x.h>

namespace cc2::api::mods {

// Replace the stock per-file content staging worker with its own raw file-copy
// primitive, and include package-root sidecars in the Steam staging directory.
// The game-build profile is exact and fail-closed.
bool InstallWorkshopUploadSupport(const cc2x_api* api);

} // namespace cc2::api::mods
