#include "ui_catalog.h"

#include "api/ui/resource_registry.h"

namespace cc2::api::vehicle {
namespace {

std::uint32_t ReadyRegion(const ui::ResourceRegistry& resources,
                          const host::VehicleDeclaration& declaration,
                          const wchar_t* role) {
  if (declaration.owner.empty() || declaration.ui.empty() || !role) return 0;
  const auto logicalId = L"vehicles/" + declaration.ui + L"/" + role;
  const auto* record = resources.Find(declaration.owner, logicalId);
  if (!record || record->descriptor.type != ui::ResourceType::TxtrSurface ||
      (record->state.load(std::memory_order_acquire) & ui::EngineReady) == 0)
    return 0;
  return record->descriptor.regionToken;
}

}  // namespace

UiRegionTokens ResolveUiRegionTokens(const host::VehicleDeclaration& declaration,
                                     const ui::ResourceRegistry& resources) {
  UiRegionTokens result;
  result.icon16 = ReadyRegion(resources, declaration, L"small");
  result.icon64 = ReadyRegion(resources, declaration, L"large");
  result.loadout64 = ReadyRegion(resources, declaration, L"loadout");
  return result;
}

}  // namespace cc2::api::vehicle
