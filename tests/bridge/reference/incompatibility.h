#pragma once

#include <cc2x.h>

namespace cc2::api::mods {

// Disables the stock auto-disable loop when a mod is enabled. The game's
// explicit enabled state is still written by update_set_is_mod_enabled.
bool InstallCompatibilityBypass(const cc2x_api* api);

} // namespace cc2::api::mods
