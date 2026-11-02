#include "resource_registry.h"

#include <algorithm>
#include <set>
#include <utility>

namespace cc2::api::ui {
namespace {

std::wstring Fold(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
    return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + (L'a' - L'A')) : c;
  });
  return value;
}

bool SafeVirtualPath(const std::string& path) {
  if (path.empty() || path.front() == '/' || path.find('\\') != std::string::npos ||
      path.find(':') != std::string::npos || path.find('\0') != std::string::npos)
    return false;
  std::size_t start = 0;
  while (start < path.size()) {
    const auto end = path.find('/', start);
    const auto part = path.substr(start, end == std::string::npos ? end : end - start);
    if (part.empty() || part == "." || part == "..") return false;
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return true;
}

bool IsSha256(const std::string& value) {
  return value.empty() || (value.size() == 64 &&
      std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F');
      }));
}

bool IsSafeWeaponType(const std::wstring& value) {
  constexpr wchar_t prefix[] = L"attachment_";
  if (value.size() <= sizeof(prefix) / sizeof(prefix[0]) - 1 ||
      value.compare(0, sizeof(prefix) / sizeof(prefix[0]) - 1, prefix) != 0)
    return false;
  return std::all_of(value.begin(), value.end(), [](wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
           (c >= L'0' && c <= L'9') || c == L'_';
  });
}

bool IsWeaponLogicalId(const std::wstring& value) {
  constexpr wchar_t prefix[] = L"weapons/";
  constexpr wchar_t largeSuffix[] = L"/large";
  constexpr wchar_t smallSuffix[] = L"/small";
  const auto prefixLength = sizeof(prefix) / sizeof(prefix[0]) - 1;
  const auto largeLength = sizeof(largeSuffix) / sizeof(largeSuffix[0]) - 1;
  const auto smallLength = sizeof(smallSuffix) / sizeof(smallSuffix[0]) - 1;
  if (value.size() <= prefixLength || value.compare(0, prefixLength, prefix) != 0)
    return false;
  return (value.size() > prefixLength + largeLength &&
          value.compare(value.size() - largeLength, largeLength, largeSuffix) == 0) ||
         (value.size() > prefixLength + smallLength &&
          value.compare(value.size() - smallLength, smallLength, smallSuffix) == 0);
}

bool HasWeaponLogicalPrefix(const std::wstring& value) {
  constexpr wchar_t prefix[] = L"weapons/";
  return value.compare(0, sizeof(prefix) / sizeof(prefix[0]) - 1, prefix) == 0;
}

}  // namespace

bool ResourceRegistry::Publish(std::vector<ResourceDescriptor> resources,
                               std::string* error) {
  if (published_.load(std::memory_order_acquire)) {
    if (error) *error = "resource registry already published";
    return false;
  }
  if (resources.size() > 16384) {
    if (error) *error = "resource descriptor limit exceeded";
    return false;
  }

  std::set<std::wstring> identities;
  std::set<std::string> virtualPaths, regionNames;
  std::set<std::uint32_t> regionTokens;
  for (const auto& resource : resources) {
    const auto identity = Fold(resource.owner) + L"\x1f" + Fold(resource.logicalId);
    const bool weaponLogicalPrefix = HasWeaponLogicalPrefix(resource.logicalId);
    if (resource.owner.empty() || resource.logicalId.empty() || resource.sourcePath.empty() ||
        !SafeVirtualPath(resource.virtualPath) || !IsSha256(resource.sourceSha256) ||
        (weaponLogicalPrefix && (!IsWeaponLogicalId(resource.logicalId) ||
                                 resource.type != ResourceType::TxtrSurface ||
                             !IsSafeWeaponType(resource.engineType))) ||
        (!weaponLogicalPrefix && !resource.engineType.empty()) ||
        !identities.insert(identity).second || !virtualPaths.insert(resource.virtualPath).second ||
        (!resource.regionName.empty() && !regionNames.insert(resource.regionName).second) ||
        (resource.regionToken != 0 && !regionTokens.insert(resource.regionToken).second)) {
      if (error) *error = "invalid or duplicate resource identity/path/region";
      return false;
    }
    if (resource.type == ResourceType::TxtrSurface &&
        (resource.regionName.empty() || resource.regionToken == 0)) {
      if (error) *error = "atlas surface requires a region name and allocated token";
      return false;
    }
  }

  auto records = std::make_unique<ResourceRecord[]>(resources.size());
  for (std::size_t i = 0; i < resources.size(); ++i) {
    records[i].descriptor = std::move(resources[i]);
    auto state = static_cast<std::uint32_t>(SourceValidated);
    if (!records[i].descriptor.generatedPath.empty()) state |= OutputGenerated;
    records[i].state.store(state, std::memory_order_relaxed);
  }
  records_ = std::move(records);
  count_ = resources.size();
  published_.store(true, std::memory_order_release);
  return true;
}

const ResourceRecord* ResourceRegistry::Find(const std::wstring& owner,
                                             const std::wstring& logicalId) const {
  if (!Published()) return nullptr;
  const auto foldedOwner = Fold(owner);
  const auto foldedId = Fold(logicalId);
  for (std::size_t i = 0; i < count_; ++i) {
    const auto& descriptor = records_[i].descriptor;
    if (Fold(descriptor.owner) == foldedOwner && Fold(descriptor.logicalId) == foldedId)
      return &records_[i];
  }
  return nullptr;
}

const ResourceRecord* ResourceRegistry::FindByVirtualPath(
    const std::string& virtualPath) const {
  if (!Published()) return nullptr;
  for (std::size_t i = 0; i < count_; ++i)
    if (records_[i].descriptor.virtualPath == virtualPath) return &records_[i];
  return nullptr;
}

const ResourceRecord* ResourceRegistry::FindByRegionName(
    const std::string& regionName) const {
  if (!Published()) return nullptr;
  for (std::size_t i = 0; i < count_; ++i)
    if (records_[i].descriptor.regionName == regionName) return &records_[i];
  return nullptr;
}

const ResourceRecord* ResourceRegistry::FindByRegionToken(std::uint32_t regionToken) const {
  if (!Published() || !regionToken) return nullptr;
  for (std::size_t i = 0; i < count_; ++i)
    if (records_[i].descriptor.regionToken == regionToken) return &records_[i];
  return nullptr;
}

const ResourceRecord* ResourceRegistry::At(std::size_t index) const {
  if (!Published() || index >= count_) return nullptr;
  return &records_[index];
}

bool ResourceRegistry::Mark(const std::wstring& owner, const std::wstring& logicalId,
                            ResourceState state) {
  auto* record = const_cast<ResourceRecord*>(Find(owner, logicalId));
  if (!record) return false;
  const auto current = record->state.load(std::memory_order_acquire);
  if (state == OutputGenerated && !(current & SourceValidated)) return false;
  if (state == EngineRequested && !(current & OutputGenerated)) return false;
  if (state == EngineReady && !(current & EngineRequested)) return false;
  record->state.fetch_or(state, std::memory_order_acq_rel);
  return true;
}

ResourceRegistry& Registry() {
  static ResourceRegistry registry;
  return registry;
}

}  // namespace cc2::api::ui
