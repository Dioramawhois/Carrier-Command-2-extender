#pragma once

#include <string>
#include "cc2x.h"

namespace cc2::runtime {

// Authenticates the executable profile and the native entry points used by
// the previously runtime-tested hangar spawn-and-dock operation.
bool ValidateHangarSpawnImage(const cc2x_api* api, std::string* reason);

}  // namespace cc2::api::carrier
