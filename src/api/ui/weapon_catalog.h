#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cc2::api::ui {

class ResourceRegistry;

struct WeaponIconToken {
  std::wstring owner;
  std::wstring identity;
  std::wstring engineType;
  std::uint32_t smallRegionToken = 0;
  std::uint32_t largeRegionToken = 0;
};

// Returns engine-ready, unambiguous owner-scoped weapon/equipment icon pairs.
// Large-only declarations remain usable with a stock small-icon fallback. A
// target type declared by more than one active owner is omitted so stock UI
// falls back to its vanilla regions instead of depending on load order.
std::vector<WeaponIconToken> ResolveWeaponIconTokens(
    const ResourceRegistry& resources,
    std::vector<std::wstring>* ambiguousEngineTypes = nullptr);

}  // namespace cc2::api::ui
