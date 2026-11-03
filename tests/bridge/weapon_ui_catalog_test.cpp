#include "api/ui/resource_registry.h"
#include "api/ui/weapon_catalog.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#define CHECK(value) do { if (!(value)) { \
  std::cerr << "FAIL " << __LINE__ << ": " << #value << '\n'; return 1; \
} } while (0)

namespace {
cc2::api::ui::ResourceDescriptor Weapon(const wchar_t* owner,
                                         const wchar_t* identity,
                                         const wchar_t* engineType,
                                         const wchar_t* role,
                                         const char* suffix,
                                         std::uint32_t token) {
  using namespace cc2::api::ui;
  ResourceDescriptor result;
  result.owner = owner;
  result.logicalId = std::wstring(L"weapons/") + identity + L"/" + role;
  result.engineType = engineType;
  result.sourcePath = L"icon.png";
  result.virtualPath = std::string("textures/cc2x_runtime/") + suffix + ".txtr";
  result.type = ResourceType::TxtrSurface;
  result.regionName = std::string("cx") + suffix;
  result.regionToken = token;
  result.generatedPath = L"cache/icon.txtr";
  return result;
}
}

int main() {
  using namespace cc2::api::ui;
  auto& registry = Registry();
  std::string error;
  std::vector<ResourceDescriptor> resources;
  resources.push_back(Weapon(L"owner.alpha", L"test_missile",
      L"attachment_hardpoint_missile_ir", L"small", "weapon_1", 0x40000001));
  resources.push_back(Weapon(L"owner.alpha", L"test_missile",
      L"attachment_hardpoint_missile_ir", L"large", "weapon_2", 0x40000002));
  resources.push_back(Weapon(L"owner.beta", L"other_missile",
      L"attachment_hardpoint_missile_ir", L"small", "weapon_3", 0x40000003));
  resources.push_back(Weapon(L"owner.beta", L"other_missile",
      L"attachment_hardpoint_missile_ir", L"large", "weapon_4", 0x40000004));
  resources.push_back(Weapon(L"owner.gamma", L"test_camera",
      L"attachment_camera_plane", L"small", "weapon_5", 0x40000005));
  resources.push_back(Weapon(L"owner.gamma", L"test_camera",
      L"attachment_camera_plane", L"large", "weapon_6", 0x40000006));
  ResourceDescriptor notReady = Weapon(L"owner.delta", L"not_ready",
      L"attachment_hardpoint_torpedo", L"large", "weapon_7", 0x40000007);
  resources.push_back(std::move(notReady));
  resources.push_back(Weapon(L"owner.legacy", L"large_only",
      L"attachment_camera_torpedo", L"large", "weapon_8", 0x40000008));
  if (!registry.Publish(std::move(resources), &error)) {
    std::cerr << "resource registry setup failed: " << error << '\n';
    return 1;
  }
  CHECK(registry.At(0) != nullptr && registry.At(99) == nullptr);
  struct MarkReady { const wchar_t* owner; const wchar_t* identity; };
  const MarkReady ready[] = {
      {L"owner.alpha", L"weapons/test_missile/small"},
      {L"owner.alpha", L"weapons/test_missile/large"},
      {L"owner.beta", L"weapons/other_missile/small"},
      {L"owner.beta", L"weapons/other_missile/large"},
      {L"owner.gamma", L"weapons/test_camera/small"},
      {L"owner.gamma", L"weapons/test_camera/large"},
      {L"owner.legacy", L"weapons/large_only/large"},
  };
  for (const auto& item : ready) {
    const auto* record = registry.Find(item.owner, item.identity);
    CHECK(record != nullptr);
    CHECK(registry.Mark(item.owner, item.identity, EngineRequested));
    CHECK(registry.Mark(item.owner, item.identity, EngineReady));
  }
  std::vector<std::wstring> ambiguous;
  const auto bindings = ResolveWeaponIconTokens(registry, &ambiguous);
  CHECK(bindings.size() == 2);
  CHECK(bindings[0].owner == L"owner.gamma");
  CHECK(bindings[0].identity == L"test_camera");
  CHECK(bindings[0].engineType == L"attachment_camera_plane");
  CHECK(bindings[0].smallRegionToken == 0x40000005);
  CHECK(bindings[0].largeRegionToken == 0x40000006);
  CHECK(bindings[1].owner == L"owner.legacy");
  CHECK(bindings[1].identity == L"large_only");
  CHECK(bindings[1].smallRegionToken == 0);
  CHECK(bindings[1].largeRegionToken == 0x40000008);
  CHECK(ambiguous.size() == 1 && ambiguous[0] == L"attachment_hardpoint_missile_ir");
  std::cout << "PASS weapon icons are owner-scoped, ready-only, paired, and collision-safe\n";
  return 0;
}
