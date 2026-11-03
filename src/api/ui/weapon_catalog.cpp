#include "weapon_catalog.h"

#include "resource_registry.h"

#include <algorithm>
#include <map>
#include <utility>

namespace cc2::api::ui {
namespace {

std::wstring Fold(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
    return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + (L'a' - L'A')) : c;
  });
  return value;
}

bool Ready(const ResourceRecord& resource) {
  return (resource.state.load(std::memory_order_acquire) & EngineReady) != 0;
}

}  // namespace

std::vector<WeaponIconToken> ResolveWeaponIconTokens(
    const ResourceRegistry& resources,
    std::vector<std::wstring>* ambiguousEngineTypes) {
  if (ambiguousEngineTypes) ambiguousEngineTypes->clear();
  struct PairKey {
    std::wstring owner;
    std::wstring identity;
    bool operator<(const PairKey& other) const {
      return owner < other.owner || (owner == other.owner && identity < other.identity);
    }
  };
  struct PairValue {
    WeaponIconToken token;
    bool hasLarge = false;
    bool invalid = false;
  };
  std::map<PairKey, PairValue> byIdentity;
  constexpr wchar_t prefix[] = L"weapons/";
  constexpr wchar_t largeSuffix[] = L"/large";
  constexpr wchar_t smallSuffix[] = L"/small";
  constexpr auto prefixLength = sizeof(prefix) / sizeof(prefix[0]) - 1;
  constexpr auto largeLength = sizeof(largeSuffix) / sizeof(largeSuffix[0]) - 1;
  constexpr auto smallLength = sizeof(smallSuffix) / sizeof(smallSuffix[0]) - 1;
  for (std::size_t i = 0; i < resources.Count(); ++i) {
    const auto* record = resources.At(i);
    if (!record || !Ready(*record) || record->descriptor.engineType.empty() ||
        record->descriptor.type != ResourceType::TxtrSurface ||
        record->descriptor.regionToken == 0) continue;
    const auto& logicalId = record->descriptor.logicalId;
    if (logicalId.size() <= prefixLength ||
        logicalId.compare(0, prefixLength, prefix) != 0) continue;
    const bool isLarge = logicalId.size() > prefixLength + largeLength &&
        logicalId.compare(logicalId.size() - largeLength, largeLength, largeSuffix) == 0;
    const bool isSmall = logicalId.size() > prefixLength + smallLength &&
        logicalId.compare(logicalId.size() - smallLength, smallLength, smallSuffix) == 0;
    if (!isLarge && !isSmall) continue;
    const auto suffixLength = isLarge ? largeLength : smallLength;
    const auto identityLength = logicalId.size() - prefixLength - suffixLength;
    if (identityLength == 0) continue;
    const auto identity = logicalId.substr(prefixLength, identityLength);
    PairKey key{Fold(record->descriptor.owner), Fold(identity)};
    auto& pair = byIdentity[key];
    if (pair.token.owner.empty()) {
      pair.token.owner = record->descriptor.owner;
      pair.token.identity = identity;
      pair.token.engineType = record->descriptor.engineType;
    } else if (Fold(pair.token.engineType) != Fold(record->descriptor.engineType)) {
      pair.invalid = true;
    }
    if (isLarge) {
      if (pair.hasLarge) pair.invalid = true;
      pair.token.largeRegionToken = record->descriptor.regionToken;
      pair.hasLarge = true;
    } else {
      if (pair.token.smallRegionToken != 0) pair.invalid = true;
      pair.token.smallRegionToken = record->descriptor.regionToken;
    }
  }

  std::map<std::wstring, std::vector<WeaponIconToken>> byType;
  for (auto& [key, pair] : byIdentity) {
    (void)key;
    if (pair.invalid || !pair.hasLarge) continue;
    byType[Fold(pair.token.engineType)].push_back(std::move(pair.token));
  }

  std::vector<WeaponIconToken> result;
  for (auto& [type, mappings] : byType) {
    if (mappings.size() != 1) {
      if (ambiguousEngineTypes) ambiguousEngineTypes->push_back(mappings.front().engineType);
      continue;
    }
    result.push_back(std::move(mappings.front()));
  }
  std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
    const auto leftType = Fold(left.engineType);
    const auto rightType = Fold(right.engineType);
    if (leftType != rightType) return leftType < rightType;
    const auto leftOwner = Fold(left.owner);
    const auto rightOwner = Fold(right.owner);
    if (leftOwner != rightOwner) return leftOwner < rightOwner;
    return Fold(left.identity) < Fold(right.identity);
  });
  return result;
}

}  // namespace cc2::api::ui
