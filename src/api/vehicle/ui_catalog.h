#pragma once

#include "runtime/early_declaration.h"

#include <cstdint>

namespace cc2::api::ui {
class ResourceRegistry;
}

namespace cc2::api::vehicle {

struct UiRegionTokens {
  std::uint32_t icon16 = 0;
  std::uint32_t icon64 = 0;
  std::uint32_t loadout64 = 0;
};

// Resolve only generated atlas regions the engine has successfully loaded.
// The declaration's explicit `ui` key is the same identity used by bridge.json.
UiRegionTokens ResolveUiRegionTokens(const host::VehicleDeclaration& declaration,
                                     const ui::ResourceRegistry& resources);

}  // namespace cc2::api::vehicle
